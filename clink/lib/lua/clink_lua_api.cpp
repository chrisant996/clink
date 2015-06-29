/* Copyright (c) 2015 Martin Ridgers
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
#include "clink_lua_api.h"
#include "core/base.h"
#include "core/str.h"
#include "lua_delegate.h"
#include "lua_script_loader.h"

extern "C" {
#include <dirent.h>
}

//------------------------------------------------------------------------------
extern "C" {
extern int              _rl_completion_case_map;
extern const char*      rl_readline_name;
}

const char*             get_clink_setting_str(const char*);
int                     get_clink_setting_int(const char*);
extern int              g_slash_translation;

//------------------------------------------------------------------------------
clink_lua_api::clink_lua_api()
{
}

//------------------------------------------------------------------------------
void clink_lua_api::initialise(struct lua_State* state)
{
    struct {
        const char* name;
        int         (clink_lua_api::*method)(lua_State*);
    } methods[] = {
// MODE4
        /*
        { "execute", lua_execute },
        */
        { "chdir",                  &clink_lua_api::change_dir },
        { "find_dirs",              &clink_lua_api::find_dirs },
        { "find_files",             &clink_lua_api::find_files },
        { "get_console_aliases",    &clink_lua_api::get_console_aliases },
        { "get_cwd",                &clink_lua_api::get_cwd },
        { "get_env",                &clink_lua_api::get_env },
        { "get_env_var_names",      &clink_lua_api::get_env_var_names },
        { "get_host_process",       &clink_lua_api::get_host_process },
        { "get_rl_variable",        &clink_lua_api::get_rl_variable },
        { "get_screen_info",        &clink_lua_api::get_screen_info },
        { "get_setting_int",        &clink_lua_api::get_setting_int },
        { "get_setting_str",        &clink_lua_api::get_setting_str },
        { "is_dir",                 &clink_lua_api::is_dir },
        { "is_rl_variable_true",    &clink_lua_api::is_rl_variable_true },
        { "lower",                  &clink_lua_api::to_lowercase },
        { "matches_are_files",      &clink_lua_api::matches_are_files },
        { "slash_translation",      &clink_lua_api::slash_translation },
        { "suppress_char_append",   &clink_lua_api::suppress_char_append },
        { "suppress_quoting",       &clink_lua_api::suppress_quoting },
// MODE4
    };

    lua_createtable(state, 0, 0);

    for (int i = 0; i < sizeof_array(methods); ++i)
    {
        lua_delegate::push(state, this, methods[i].method);
        lua_setfield(state, -2, methods[i].name);
    }

    lua_setglobal(state, "clink");

    lua_load_script(state, lib, clink);
}

//------------------------------------------------------------------------------
int clink_lua_api::change_dir(lua_State* state)
{
    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
        return 0;

    const char* path = lua_tostring(state, 1);
    SetCurrentDirectory(path);

    return 0;
}

//------------------------------------------------------------------------------
int clink_lua_api::to_lowercase(lua_State* state)
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
int clink_lua_api::find_files_impl(lua_State* state, int dirs_only)
{
    // Check arguments.
    int i = lua_gettop(state);
    if (i == 0 || lua_isnil(state, 1))
        return 0;

    const char* mask = lua_tostring(state, 1);

    // Should the mask be adjusted for -/_ case mapping?
    str<512> buffer;
    if (_rl_completion_case_map && i > 1 && lua_toboolean(state, 2))
    {
        buffer << mask;
        mask = buffer.c_str();

        char* slash;
        slash = strrchr(buffer.data(), '\\');
        slash = slash ? slash : strrchr(buffer.data(), '/');
        slash = slash ? slash + 1 : buffer.data();

        while (*slash)
        {
            char c = *slash;
            if (c == '_' || c == '-')
                *slash = '?';

            ++slash;
        }
    }

    lua_createtable(state, 0, 0);

    i = 1;
    DIR* dir = opendir(mask);
    while (struct dirent* entry = readdir(dir))
    {
        if (dirs_only && !(entry->attrib & _A_SUBDIR))
            continue;

        lua_pushstring(state, entry->d_name);
        lua_rawseti(state, -2, i++);
    }
    closedir(dir);

    return 1;
}

//------------------------------------------------------------------------------
int clink_lua_api::find_files(lua_State* state)
{
    return find_files_impl(state, 0);
}

//------------------------------------------------------------------------------
int clink_lua_api::find_dirs(lua_State* state)
{
    return find_files_impl(state, 1);
}

//------------------------------------------------------------------------------
int clink_lua_api::matches_are_files(lua_State* state)
{
    int i = 1;

    if (lua_gettop(state) > 0)
        i = (int)lua_tointeger(state, 1);

#if MODE4
    rl_filename_completion_desired = i;
#endif
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
    size = GetEnvironmentVariable(name, nullptr, 0);
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
int clink_lua_api::get_env_var_names(lua_State* state)
{
    char* env_strings;
    int i = 1;

    lua_createtable(state, 0, 0);
    env_strings = GetEnvironmentStrings();
    if (env_strings != nullptr)
    {
        char* string = env_strings;

        while (*string)
        {
            char* eq = strchr(string, L'=');
            if (eq != nullptr)
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
int clink_lua_api::get_setting_str(lua_State* state)
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
int clink_lua_api::get_setting_int(lua_State* state)
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
int clink_lua_api::suppress_char_append(lua_State* state)
{
#if MODE4
    rl_completion_suppress_append = 1;
#endif
    return 0;
}

//------------------------------------------------------------------------------
int clink_lua_api::suppress_quoting(lua_State* state)
{
#if MODE4
    rl_completion_suppress_quote = 1;
#endif
    return 0;
}

//------------------------------------------------------------------------------
int clink_lua_api::slash_translation(lua_State* state)
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
int clink_lua_api::is_dir(lua_State* state)
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
int clink_lua_api::get_rl_variable(lua_State* state)
{
    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
        return 0;

#if MODE4
    const char* string = lua_tostring(state, 1);
    const char* rl_cvar = rl_variable_value(string);
    if (rl_cvar == nullptr)
        return 0;

    lua_pushstring(state, rl_cvar);
    return 1;
#else
    return 0;
#endif // MODE4
}

//------------------------------------------------------------------------------
int clink_lua_api::is_rl_variable_true(lua_State* state)
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
int clink_lua_api::get_host_process(lua_State* state)
{
    lua_pushstring(state, rl_readline_name);
    return 1;
}

//------------------------------------------------------------------------------
int clink_lua_api::get_cwd(lua_State* state)
{
    char path[MAX_PATH];

    GetCurrentDirectory(sizeof_array(path), path);
    lua_pushstring(state, path);
    return 1;
}

//------------------------------------------------------------------------------
int clink_lua_api::get_console_aliases(lua_State* state)
{
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

        char* buffer = (char*)malloc(buffer_size + 1);
        if (GetConsoleAliases(buffer, buffer_size, (char*)rl_readline_name) == 0)
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
            if (c == nullptr)
            {
                break;
            }

            *c = '\0';
            lua_pushstring(state, alias);
            lua_rawseti(state, -2, i++);

            ++c;
            alias = c + strlen(c) + 1;
        }

        free(buffer);
#endif // !__MINGW32__ && !__MINGW64__
    }
    while (0);

    return 1;
}

//------------------------------------------------------------------------------
int clink_lua_api::get_screen_info(lua_State* state)
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
