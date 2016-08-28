// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_script_loader.h"

#include <core/settings.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
static setting_bool g_debug(
    "lua.debug",
    "Enables Lua debugging.",
    "Loads an simple embedded command line debugger when enabled. Breakpoints\n"
    "can added by calling pause() in your scripts. The debugger will automatically\n"
    "break when an error's encountered.",
    false);



//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state&);
void os_lua_initialise(lua_state&);
void path_lua_initialise(lua_state&);
void settings_lua_initialise(lua_state&);
void string_lua_initialise(lua_state&);



//------------------------------------------------------------------------------
lua_state::lua_state()
: m_state(nullptr)
{
    initialise();
}

//------------------------------------------------------------------------------
lua_state::~lua_state()
{
    shutdown();
}

//------------------------------------------------------------------------------
void lua_state::initialise()
{
    shutdown();

    // Create a new Lua state.
    m_state = luaL_newstate();
    luaL_openlibs(m_state);

    lua_state& self = *this;
    clink_lua_initialise(self);
    os_lua_initialise(self);
    path_lua_initialise(self);
    settings_lua_initialise(self);
    string_lua_initialise(self);

    if (g_debug.get())
        lua_load_script(self, lib, debugger);
}

//------------------------------------------------------------------------------
void lua_state::shutdown()
{
    if (m_state == nullptr)
        return;

    lua_close(m_state);
    m_state = nullptr;
}

//------------------------------------------------------------------------------
bool lua_state::do_string(const char* string)
{
    bool failed;
    if (failed = !!luaL_dostring(m_state, string))
        if (const char* error = lua_tostring(m_state, -1))
            puts(error);

    lua_settop(m_state, 0);
    return !failed;
}

//------------------------------------------------------------------------------
bool lua_state::do_file(const char* path)
{
    bool failed;
    if (failed = !!luaL_dofile(m_state, path))
        if (const char* error = lua_tostring(m_state, -1))
            puts(error);

    lua_settop(m_state, 0);
    return !failed;
}
