// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_script_loader.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state&);
void os_lua_initialise(lua_state&);
void path_lua_initialise(lua_state&);
void settings_lua_initialise(lua_state&);

//------------------------------------------------------------------------------
lua_state::lua_state(bool enable_debugger)
: m_state(nullptr)
, m_enable_debugger(enable_debugger)
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

    clink_lua_initialise(*this);
    os_lua_initialise(*this);
    path_lua_initialise(*this);
    settings_lua_initialise(*this);

    if (m_enable_debugger)
        lua_load_script(*this, lib, debugger);
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
    if (failed = !!luaL_loadstring(m_state, string))
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
