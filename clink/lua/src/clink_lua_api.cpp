// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_script_loader.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/path.h>
#include <core/str.h>

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
