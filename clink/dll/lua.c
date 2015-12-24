/* Copyright (c) 2012 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "inject_args.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
static int              reload_lua_state(int count, int invoking_key);
const char*             get_clink_setting_str(const char*);
int                     get_clink_setting_int(const char*);
int                     rl_add_funmap_entry(const char*, int (*)(int, int));
int                     lua_execute(lua_State* state);

extern inject_args_t    g_inject_args;
extern int              rl_filename_completion_desired;
extern int              rl_completion_suppress_append;
extern char*            rl_line_buffer;
extern int              rl_point;
extern int              _rl_completion_case_map;
extern int              g_slash_translation;
extern char*            rl_variable_value(const char*);
static lua_State*       g_lua                        = NULL;

//------------------------------------------------------------------------------
static void load_lua_script(const char* script)
{
    if (luaL_dofile(g_lua, script) != 0)
    {
        const char* error_msg = lua_tostring(g_lua, -1);
        fputs(error_msg, stderr);
        fputs("\n", stderr);
        lua_pop(g_lua, 1);
    }
}

//------------------------------------------------------------------------------
static void load_lua_scripts(const char* path)
{
    int i;
    char path_buf[1024];
    HANDLE find;
    WIN32_FIND_DATA fd;

    str_cpy(path_buf, path, sizeof_array(path_buf));
    str_cat(path_buf, "\\", sizeof_array(path_buf));
    i = strlen(path_buf);

    str_cat(path_buf, "*.lua", sizeof_array(path_buf));
    find = FindFirstFile(path_buf, &fd);
    path_buf[i] = '\0';

    while (find != INVALID_HANDLE_VALUE)
    {
        if (_stricmp(fd.cFileName, "clink.lua") != 0)
        {
            str_cat(path_buf, fd.cFileName, sizeof_array(path_buf));
            load_lua_script(path_buf);
            path_buf[i] = '\0';
        }

        if (FindNextFile(find, &fd) == FALSE)
        {
            FindClose(find);
            break;
        }
    }
}

//------------------------------------------------------------------------------
char** lua_match_display_filter(char** matches, int match_count)
{
    // A small note about the contents of 'matches' - the first match isn't
    // really a match, it's the word being completed. Readline ignores it when
    // displaying the matches. So matches[1...n] are useful.

    char** new_matches;
    int top;
    int i;

    top = lua_gettop(g_lua);
    new_matches = NULL;

    // Check there's a display filter set.
    lua_getglobal(g_lua, "clink");

    lua_pushliteral(g_lua, "match_display_filter");
    lua_rawget(g_lua, -2);

    i = lua_isnil(g_lua, -1);

    if (i != 0)
    {
        goto done;
    }

    // Convert matches to a Lua table.
    lua_createtable(g_lua, match_count, 0);
    for (i = 1; i < match_count; ++i)
    {
        lua_pushstring(g_lua, matches[i]);
        lua_rawseti(g_lua, -2, i);
    }

    // Call the filter.
    if (lua_pcall(g_lua, 1, 1, 0) != 0)
    {
        puts(lua_tostring(g_lua, -1));
        goto done;
    }

    // Convert table returned by the Lua filter function to C.
    new_matches = (char**)calloc(match_count + 1, sizeof(*new_matches));
    for (i = 0; i < match_count; ++i)
    {
        const char* match;

        lua_rawgeti(g_lua, -1, i);
        if (lua_isnil(g_lua, -1))
        {
            match = "nil";
        }
        else
        {
            match = lua_tostring(g_lua, -1);
        }

        new_matches[i] = malloc(strlen(match) + 1);
        strcpy(new_matches[i], match);

        lua_pop(g_lua, 1);
    }

done:
    top = lua_gettop(g_lua) - top;
    lua_pop(g_lua, top);
    return new_matches;
}

//------------------------------------------------------------------------------
static int to_lowercase(lua_State* state)
{
    const char* string;
    char* lowered;
    int length;
    int i;

    // Check we've got at least one argument...
    if (lua_gettop(state) == 0)
    {
        return 0;
    }

    // ...and that the argument is a string.
    if (!lua_isstring(state, 1))
    {
        return 0;
    }
    
    string = lua_tostring(state, 1);
    length = (int)strlen(string);

    lowered = (char*)malloc(length + 1);
    if (_rl_completion_case_map)
    {
        for (i = 0; i <= length; ++i)
        {
            char c = string[i];
            if (c == '-')
            {
                c = '_';
            }
            else
            {
                c = tolower(c);
            }

            lowered[i] = c;
        }
    }
    else
    {
        for (i = 0; i <= length; ++i)
        {
            char c = string[i];
            lowered[i] = tolower(c);
        }
    }

    lua_pushstring(state, lowered);
    free(lowered);

    return 1;
}

//------------------------------------------------------------------------------
static int find_files_impl(lua_State* state, int dirs_only)
{
    DIR* dir;
    struct dirent* entry;
    char buffer[512];
    const char* mask;
    const char* mask_file;
    int i;
    int case_map;

    // Check arguments.
    i = lua_gettop(state);
    if (i == 0 || lua_isnil(state, 1))
    {
        return 0;
    }

    mask = lua_tostring(state, 1);
    mask_file = NULL;

    // Should the mask be adjusted for -/_ case mapping?
    if (_rl_completion_case_map && i > 1 && lua_toboolean(state, 2))
    {
        char* slash;

        str_cpy(buffer, mask, sizeof_array(buffer));
        mask = buffer;

        slash = strrchr(buffer, '\\');
        slash = slash ? slash : strrchr(buffer, '/');
        slash = slash ? slash + 1 : buffer;

        while (*slash)
        {
            char c = *slash;
            if (c == '_' || c == '-')
            {
                *slash = '?';
            }

            ++slash;
        }

        mask_file = strrchr(mask, '\\');
        if (mask_file == NULL)
            mask_file = strrchr(mask, '/');
        mask_file = (mask_file == NULL) ? mask : mask_file + 1;
    }
    
    lua_createtable(state, 0, 0);

    i = 1;
    dir = opendir(mask);
    while (entry = readdir(dir))
    {
        if (dirs_only && !(entry->attrib & _A_SUBDIR))
        {
            continue;
        }

        // Check the returned files against the '?' wildcard in mask for -/_
        // if the matches should be case insensitive.
        if (mask_file != NULL)
        {
            int ok = 1;
            const char* read = mask_file;

            while (*read != '\0')
            {
                char c = *read;
                if (c == '?')
                {
                    char d = entry->d_name[read - mask_file];
                    if (d != '-' && d != '_')
                    {
                        ok = 0;
                        break;
                    }
                }
                else if (c == '*')
                    break;

                ++read;
            }

            if (!ok)
                continue;
        }

        lua_pushstring(state, entry->d_name);
        lua_rawseti(state, -2, i++);
    }
    closedir(dir);

    return 1;
}

//------------------------------------------------------------------------------
static int find_files(lua_State* state)
{
    return find_files_impl(state, 0);
}

//------------------------------------------------------------------------------
static int find_dirs(lua_State* state)
{
    return find_files_impl(state, 1);
}

//------------------------------------------------------------------------------
static int matches_are_files(lua_State* state)
{
    int i = 1;

    if (lua_gettop(state) > 0)
    {
        i = lua_tointeger(state, 1);
    }

    rl_filename_completion_desired = i;
    return 0;
}

//------------------------------------------------------------------------------
static char* mbcs_to_utf8(char* buff)
{
    wchar_t* buf_wchar;
    char* buf_utf8;
    int len_wchar, len_utf8;
    
    // Convert MBCS to WideChar.
    len_wchar = MultiByteToWideChar(CP_ACP, 0, buff, -1, NULL, 0);
    buf_wchar = (wchar_t*)malloc((len_wchar + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_ACP, 0, buff, -1, buf_wchar, len_wchar);

    // Convert WideChar to UTF8.
    len_utf8 = WideCharToMultiByte(CP_UTF8, 0, buf_wchar, len_wchar, NULL, 0, NULL, NULL);
    buf_utf8 = (char*)malloc(len_utf8 + 1);
    WideCharToMultiByte(CP_UTF8, 0, buf_wchar, len_wchar, buf_utf8, len_utf8, NULL, NULL);

    free(buf_wchar);
    return buf_utf8;
}

//------------------------------------------------------------------------------
static int get_env(lua_State* state)
{
    unsigned size;
    const char* name;
    char* buffer;
    char* buf_utf8;

    if (lua_gettop(state) == 0)
    {
        return 0;
    }

    if (lua_isnil(state, 1))
    {
        return 0;
    }

    name = lua_tostring(state, 1);
    size = GetEnvironmentVariable(name, NULL, 0);
    if (!size)
    {
        return 0;
    }
    
    buffer = (char*)malloc(size);
    GetEnvironmentVariable(name, buffer, size);
    buf_utf8 = mbcs_to_utf8(buffer);
    lua_pushstring(state, buf_utf8);
    free(buf_utf8);
    free(buffer);
    
    return 1;
}

//------------------------------------------------------------------------------
static int get_env_var_names(lua_State* state)
{
    char* env_strings;
    int i = 1;

    lua_createtable(state, 0, 0);
    env_strings = GetEnvironmentStrings();
    if (env_strings != NULL)
    {
        char* string = env_strings;

        while (*string)
        {
            char* eq = strchr(string, L'=');
            if (eq != NULL)
            {
                size_t length = eq - string + 1;
                char name[1024];

                length = length < sizeof_array(name) ? length : sizeof_array(name);
                --length;
                if (length > 0)
                {
                    strncpy(name, string, length);
                    name[length] = L'\0';

                    lua_pushstring(state, name);
                    lua_rawseti(state, -2, i++);
                }
            }

            string += strlen(string) + 1;
        }

        FreeEnvironmentStrings(env_strings);
    }

    return 1;
}

//------------------------------------------------------------------------------
static int get_setting_str(lua_State* state)
{
    const char* c;

    if (lua_gettop(state) == 0)
    {
        return 0;
    }

    if (lua_isnil(state, 1) || !lua_isstring(state, 1))
    {
        return 0;
    }

    c = lua_tostring(state, 1);
    c = get_clink_setting_str(c);
    lua_pushstring(state, c);

    return 1;
}

//------------------------------------------------------------------------------
static int get_setting_int(lua_State* state)
{
    int i;
    const char* c;

    if (lua_gettop(state) == 0)
    {
        return 0;
    }

    if (lua_isnil(state, 1) || !lua_isstring(state, 1))
    {
        return 0;
    }

    c = lua_tostring(state, 1);
    i = get_clink_setting_int(c);
    lua_pushinteger(state, i);

    return 1;
}

//------------------------------------------------------------------------------
static int suppress_char_append(lua_State* state)
{
    rl_completion_suppress_append = 1;
    return 0;
}

//------------------------------------------------------------------------------
static int suppress_quoting(lua_State* state)
{
    rl_completion_suppress_quote = 1;
    return 0;
}

//------------------------------------------------------------------------------
static int slash_translation(lua_State* state)
{
    if (lua_gettop(state) == 0)
    {
        g_slash_translation = 0;
    }
    else
    {
        g_slash_translation = (int)lua_tointeger(state, 1);
    }

    return 0;
}

//------------------------------------------------------------------------------
static int is_dir(lua_State* state)
{
    const char* name;
    DWORD attrib;
    int i;

    if (lua_gettop(state) == 0)
    {
        return 0;
    }

    if (lua_isnil(state, 1))
    {
        return 0;
    }

    i = 0;
    name = lua_tostring(state, 1);
    attrib = GetFileAttributes(name);
    if (attrib != INVALID_FILE_ATTRIBUTES)
    {
        i = !!(attrib & FILE_ATTRIBUTE_DIRECTORY);
    }

    lua_pushboolean(state, i);

    return 1;
}

//------------------------------------------------------------------------------
static int get_rl_variable(lua_State* state)
{
    const char* string;
    const char* rl_cvar;

    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
    {
        return 0;
    }

    string = lua_tostring(state, 1);
    rl_cvar = rl_variable_value(string);
    if (rl_cvar == NULL)
    {
        return 0;
    }

    lua_pushstring(state, rl_cvar);
    return 1;
}

//------------------------------------------------------------------------------
static int is_rl_variable_true(lua_State* state)
{
    int i;
    const char* cvar_value;

    i = get_rl_variable(state);
    if (i == 0)
    {
        return 0;
    }

    cvar_value = lua_tostring(state, -1);
    i = (_stricmp(cvar_value, "on") == 0) || (_stricmp(cvar_value, "1") == 0);
    lua_pop(state, 1);
    lua_pushboolean(state, i);

    return 1;
}

//------------------------------------------------------------------------------
static int get_host_process(lua_State* state)
{
    lua_pushstring(state, rl_readline_name);
    return 1;
}

//------------------------------------------------------------------------------
static int change_dir(lua_State* state)
{
    const char* path;

    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
    {
        return 0;
    }

    path = lua_tostring(state, 1);
    SetCurrentDirectory(path);

    return 0;
}

//------------------------------------------------------------------------------
static int get_cwd(lua_State* state)
{
    char path[MAX_PATH];

    GetCurrentDirectory(sizeof_array(path), path);
    lua_pushstring(state, path);
    return 1; 
}

//------------------------------------------------------------------------------
static int get_console_aliases(lua_State* state)
{
    char* buffer = NULL;

    do
    {
        int i;
        int buffer_size;
        char* alias;

        lua_createtable(state, 0, 0);

#if !defined(__MINGW32__) && !defined(__MINGW64__)
        // Get the aliases (aka. doskey macros).
        buffer_size = GetConsoleAliasesLength((char*)rl_readline_name);
        if (buffer_size == 0)
        {
            break;
        }

        buffer = malloc(buffer_size + 1);
        if (GetConsoleAliases(buffer, buffer_size, rl_readline_name) == 0)
        {
            break;
        }

        buffer[buffer_size] = '\0';

        // Parse the result into a lua table.
        alias = buffer;
        i = 1;
        while (*alias != '\0')
        {
            char* c = strchr(alias, '=');
            if (c == NULL)
            {
                break;
            }

            *c = '\0';
            lua_pushstring(state, alias);
            lua_rawseti(state, -2, i++);

            ++c;
            alias = c + strlen(c) + 1;
        }
#endif // !__MINGW32__ && !__MINGW64__
    }
    while (0);

    free(buffer);
    return 1;
}

//------------------------------------------------------------------------------
static int get_screen_info(lua_State* state)
{
    int i;
    int buffer_width, buffer_height;
    int window_width, window_height;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    struct table_t {
        const char* name;
        int         value;
    };

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    buffer_width = csbi.dwSize.X;
    buffer_height = csbi.dwSize.Y;
    window_width = csbi.srWindow.Right - csbi.srWindow.Left;
    window_height = csbi.srWindow.Bottom - csbi.srWindow.Top;

    lua_createtable(state, 0, 4);
    {
        struct table_t table[] = {
            { "buffer_width", buffer_width },
            { "buffer_height", buffer_height },
            { "window_width", window_width },
            { "window_height", window_height },
        };

        for (i = 0; i < sizeof_array(table); ++i)
        {
            lua_pushstring(state, table[i].name);
            lua_pushinteger(state, table[i].value);
            lua_rawset(state, -3);
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
lua_State* initialise_lua()
{
    static int once = 0;
    int i;
    int path_hash;
    char buffer[1024];
    struct luaL_Reg clink_native_methods[] = {
        { "chdir", change_dir },
        { "execute", lua_execute },
        { "find_dirs", find_dirs },
        { "find_files", find_files },
        { "get_console_aliases", get_console_aliases },
        { "get_cwd", get_cwd },
        { "get_env", get_env },
        { "get_env_var_names", get_env_var_names },
        { "get_host_process", get_host_process },
        { "get_rl_variable", get_rl_variable },
        { "get_screen_info", get_screen_info },
        { "get_setting_int", get_setting_int },
        { "get_setting_str", get_setting_str },
        { "is_dir", is_dir },
        { "is_rl_variable_true", is_rl_variable_true },
        { "lower", to_lowercase },
        { "matches_are_files", matches_are_files },
        { "slash_translation", slash_translation },
        { "suppress_char_append", suppress_char_append },
        { "suppress_quoting", suppress_quoting },
        { NULL, NULL }
    };

    if (g_lua != NULL)
    {
        return g_lua;
    }

    // Initialise Lua.
    g_lua = luaL_newstate();
    luaL_openlibs(g_lua);

    // Add our API.
    lua_createtable(g_lua, 0, 0);
    lua_setglobal(g_lua, "clink");

    lua_getglobal(g_lua, "clink");
    luaL_setfuncs(g_lua, clink_native_methods, 0);
    lua_pop(g_lua, 1);

    // Load all the .lua files alongside the dll and in the script folder.
    if (g_inject_args.script_path[0] == '\0')
    {
        get_dll_dir(buffer, sizeof_array(buffer));
    }
    else
    {
        buffer[0] = '\0';
        str_cat(buffer, g_inject_args.script_path, sizeof_array(buffer));
    }

    path_hash = hash_string(buffer);
    i = (int)strlen(buffer);

    str_cat(buffer, "/clink.lua", sizeof_array(buffer));
    load_lua_script(buffer);

    buffer[i] = '\0';
    load_lua_scripts(buffer);

    get_config_dir(buffer, sizeof(buffer));
    if (hash_string(buffer) != path_hash)
    {
        load_lua_scripts(buffer);
    }

    if (!once)
    {
        rl_add_funmap_entry("reload-lua-state", reload_lua_state);
        once = 1;
    }

    return g_lua;
}

//------------------------------------------------------------------------------
void shutdown_lua()
{
    if (g_lua == NULL)
    {
        return;
    }

    lua_close(g_lua);
    g_lua = NULL;
}

//------------------------------------------------------------------------------
char** lua_generate_matches(const char* text, int start, int end)
{
    int match_count;
    int use_matches;
    int i;
    char** matches = NULL;

    // Expose some of the readline state to lua.
    lua_createtable(g_lua, 2, 0);

    lua_pushliteral(g_lua, "line_buffer");
    lua_pushstring(g_lua, rl_line_buffer);
    lua_rawset(g_lua, -3);

    lua_pushliteral(g_lua, "point");
    lua_pushinteger(g_lua, rl_point + 1);
    lua_rawset(g_lua, -3);

    lua_setglobal(g_lua, "rl_state");

    // Call to Lua to generate matches.
    lua_getglobal(g_lua, "clink");
    lua_pushliteral(g_lua, "generate_matches");
    lua_rawget(g_lua, -2);

    lua_pushstring(g_lua, text);
    lua_pushinteger(g_lua, start + 1);
    lua_pushinteger(g_lua, end);
    if (lua_pcall(g_lua, 3, 1, 0) != 0)
    {
        puts(lua_tostring(g_lua, -1));
        lua_pop(g_lua, 2);
        return NULL;
    }

    use_matches = lua_toboolean(g_lua, -1);
    lua_pop(g_lua, 1);

    if (use_matches == 0)
    {
        lua_pop(g_lua, 1);
        return NULL;
    }

    // Collect matches from Lua.
    lua_pushliteral(g_lua, "matches");
    lua_rawget(g_lua, -2);

    match_count = (int)lua_rawlen(g_lua, -1);
    matches = (char**)calloc(match_count + 1, sizeof(*matches));
    for (i = 0; i < match_count; ++i)
    {
        const char* match;

        lua_rawgeti(g_lua, -1, i + 1);
        match = lua_tostring(g_lua, -1);
        matches[i] = malloc(strlen(match) + 1);
        strcpy(matches[i], match);

        lua_pop(g_lua, 1);
    }
    lua_pop(g_lua, 2);

    return matches;
}

//------------------------------------------------------------------------------
static int reload_lua_state(int count, int invoking_key)
{
    shutdown_lua();
    initialise_lua();
    return 1;
}

//------------------------------------------------------------------------------
void lua_filter_prompt(char* buffer, int buffer_size)
{
    const char* prompt;

    // Call Lua to filter prompt
    lua_getglobal(g_lua, "clink");
    lua_pushliteral(g_lua, "filter_prompt");
    lua_rawget(g_lua, -2);

    lua_pushstring(g_lua, buffer);
    if (lua_pcall(g_lua, 1, 1, 0) != 0)
    {
        puts(lua_tostring(g_lua, -1));
        lua_pop(g_lua, 2);
        return;
    }

    // Collect the filtered prompt.
    prompt = lua_tostring(g_lua, -1);
    buffer[0] = '\0';
    str_cat(buffer, prompt, buffer_size);

    lua_pop(g_lua, 2);
}

// vim: expandtab
