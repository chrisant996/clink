// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_root.h"
#include "lua_script_loader.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

//------------------------------------------------------------------------------
lua_root::lua_root()
: m_state(nullptr)
{
    initialise();
}

//------------------------------------------------------------------------------
lua_root::~lua_root()
{
    shutdown();
}

//------------------------------------------------------------------------------
void lua_root::initialise()
{
    shutdown();

    // Create a new Lua state.
    m_state = luaL_newstate();
    luaL_openlibs(m_state);

    m_clink.initialise(m_state);
    m_path.initialise(m_state);

    lua_match_generator::initialise(m_state);

    lua_load_script(m_state, lib, prompt);
    lua_load_script(m_state, lib, arguments);
#ifdef _DEBUG
    lua_load_script(m_state, lib, debugger);
#endif
}

//------------------------------------------------------------------------------
void lua_root::shutdown()
{
    if (m_state == nullptr)
        return;

    lua_match_generator::shutdown();
    lua_close(m_state);
    m_state = nullptr;
}

//------------------------------------------------------------------------------
bool lua_root::do_string(const char* string)
{
    if (luaL_dostring(m_state, string))
        return false;

    return true;
}

//------------------------------------------------------------------------------
bool lua_root::do_file(const char* path)
{
    if (luaL_dofile(m_state, path))
        return false;

    return true;
}
