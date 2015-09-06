// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clink_lua_api.h"
#include "core/base.h"
#include "core/str.h"
#include "lua_delegate.h"
#include "lua_script_loader.h"

//------------------------------------------------------------------------------
extern "C" {
extern int              _rl_completion_case_map;
extern const char*      rl_readline_name;
}

const char*             get_clink_setting_str(const char*);
int                     get_clink_setting_int(const char*);
extern int              g_slash_translation;

//------------------------------------------------------------------------------
void clink_lua_api::initialise(struct lua_State* state)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
// MODE4
        /*
        { "execute", lua_execute },
        */
        { "get_console_aliases",    &clink_lua_api::get_console_aliases },
        { "get_host_process",       &clink_lua_api::get_host_process },
        { "get_rl_variable",        &clink_lua_api::get_rl_variable },
        { "get_screen_info",        &clink_lua_api::get_screen_info },
        { "get_setting_int",        &clink_lua_api::get_setting_int },
        { "get_setting_str",        &clink_lua_api::get_setting_str },
        { "is_rl_variable_true",    &clink_lua_api::is_rl_variable_true },
        { "lower",                  &clink_lua_api::to_lowercase },
        { "matches_are_files",      &clink_lua_api::matches_are_files },
// MODE4
    };

    lua_createtable(state, 0, 0);

    for (int i = 0; i < sizeof_array(methods); ++i)
    {
        lua_pushcfunction(state, methods[i].method);
        lua_setfield(state, -2, methods[i].name);
    }

    lua_setglobal(state, "clink");

    lua_load_script(state, lib, clink);
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
        return 0;

    // ...and that the argument is a string.
    if (!lua_isstring(state, 1))
        return 0;

    string = lua_tostring(state, 1);
    length = (int)strlen(string);

    lowered = (char*)malloc(length + 1);
    if (_rl_completion_case_map)
    {
        for (i = 0; i <= length; ++i)
        {
            char c = string[i];
            if (c == '-')
                c = '_';
            else
                c = tolower(c);

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
int clink_lua_api::get_setting_str(lua_State* state)
{
    const char* c;

    if (lua_gettop(state) == 0)
        return 0;

    if (lua_isnil(state, 1) || !lua_isstring(state, 1))
        return 0;

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
        return 0;

    if (lua_isnil(state, 1) || !lua_isstring(state, 1))
        return 0;

    c = lua_tostring(state, 1);
    i = get_clink_setting_int(c);
    lua_pushinteger(state, i);

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
        return 0;

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
            break;

        char* buffer = (char*)malloc(buffer_size + 1);
        if (GetConsoleAliases(buffer, buffer_size, (char*)rl_readline_name) == 0)
            break;

        buffer[buffer_size] = '\0';

        // Parse the result into a lua table.
        alias = buffer;
        i = 1;
        while (*alias != '\0')
        {
            char* c = strchr(alias, '=');
            if (c == nullptr)
                break;

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
