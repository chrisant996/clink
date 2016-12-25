// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_script_loader.h"

#include <core/settings.h>
#include <core/os.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
static setting_bool g_lua_debug(
    "lua.debug",
    "Enables Lua debugging",
    "Loads an simple embedded command line debugger when enabled. Breakpoints\n"
    "can added by calling pause().",
    false);

static setting_str g_lua_path(
    "lua.path",
    "'require' search path",
    "Value to append to package.path. Used to search for Lua scripts specified\n"
    "in require() statements.",
    "");



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

    // Set up the package.path value for require() statements.
    str<280> path;
    if (!os::get_env("lua_path_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR, path))
        os::get_env("lua_path", path);

    const char* p = g_lua_path.get();
    if (*p)
    {
        if (!path.empty())
            path << ";";

        path << p;
    }

    if (!path.empty())
    {
        lua_getglobal(m_state, "package");
        lua_pushstring(m_state, "path");
        lua_pushstring(m_state, path.c_str());
        lua_rawset(m_state, -3);
    }

    lua_state& self = *this;

    if (g_lua_debug.get())
        lua_load_script(self, lib, debugger);

    clink_lua_initialise(self);
    os_lua_initialise(self);
    path_lua_initialise(self);
    settings_lua_initialise(self);
    string_lua_initialise(self);
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
bool lua_state::do_string(const char* string, int length)
{
    if (length < 0)
        length = int(strlen(string));

    bool ok;
    if (ok = !luaL_loadbuffer(m_state, string, length, string))
        ok = !lua_pcall(m_state, 0, LUA_MULTRET, 0);

    if (!ok)
        if (const char* error = lua_tostring(m_state, -1))
            puts(error);

    lua_settop(m_state, 0);
    return ok;
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
