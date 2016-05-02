// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_script_loader.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/path.h>
#include <core/str.h>

#include <ctype.h>
#include <Windows.h>

//------------------------------------------------------------------------------
static bool get_host_process(str_base& out)
{
    // MODE4
    str<288> buffer;
    if (GetModuleFileName(nullptr, buffer.data(), buffer.size()) == buffer.size())
        return false;

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return false;

    return path::get_name(buffer.c_str(), out);
}

//------------------------------------------------------------------------------
static int get_host_process(lua_State* state)
{
    str<64> name;
    if (get_host_process(name))
    {
        lua_pushstring(state, name.c_str());
        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
static int get_console_aliases(lua_State* state)
{
    lua_createtable(state, 0, 0);

#if !defined(__MINGW32__) && !defined(__MINGW64__)
    str<64> name;
    if (!get_host_process(name))
        return 1;

    // Get the aliases (aka. doskey macros).
    int buffer_size = GetConsoleAliasesLength(name.data());
    if (buffer_size == 0)
        return 1;

    char* buffer = (char*)malloc(buffer_size + 1);
    if (GetConsoleAliases(buffer, buffer_size, name.data()) == 0)
        return 1;

    buffer[buffer_size] = '\0';

    // Parse the result into a lua table.
    char* alias = buffer;
    int i = 1;
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
#endif // __MINGW32__
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
void clink_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
// MODE4
        /*
        { "execute", lua_execute },
        */

        // MODE4 : nomenclature should match Lua's
        { "get_console_aliases",    &get_console_aliases },
        { "get_host_process",       &get_host_process },
        { "get_screen_info",        &get_screen_info },
// MODE4
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (int i = 0; i < sizeof_array(methods); ++i)
    {
        lua_pushcfunction(state, methods[i].method);
        lua_setfield(state, -2, methods[i].name); // MODE4 - lua_rawset?
    }

    lua_setglobal(state, "clink");

    lua_load_script(lua, lib, clink);
}
