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
bool g_force_load_debugger = false;

//------------------------------------------------------------------------------
static setting_bool g_lua_debug(
    "lua.debug",
    "Enables Lua debugging",
    "Loads a simple embedded command line debugger when enabled.\n"
    "The debugger can be activated by inserting a pause() call, which will act\n"
    "as a breakpoint. Or the debugger can be activated by traceback() calls or\n"
    "Lua errors by turning on the lua.break_on_traceback or lua.break_on_error\n"
    "settings, respectively.",
    false);

static setting_str g_lua_path(
    "lua.path",
    "'require' search path",
    "Value to append to package.path. Used to search for Lua scripts specified\n"
    "in require() statements.",
    "");

static setting_bool g_lua_tracebackonerror(
    "lua.traceback_on_error",
    "Prints stack trace on Lua errors",
    false);

static setting_bool g_lua_breakontraceback(
    "lua.break_on_traceback",
    "Breaks into Lua debugger on traceback",
    "Breaks into the Lua debugger on traceback() calls, if lua.debug is enabled.",
    false);

static setting_bool g_lua_breakonerror(
    "lua.break_on_error",
    "Breaks into Lua debugger on Lua errors",
    "Breaks into the Lua debugger on Lua errors, if lua.debug is enabled.",
    false);



//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state&);
void os_lua_initialise(lua_state&);
void path_lua_initialise(lua_state&);
void settings_lua_initialise(lua_state&);
void string_lua_initialise(lua_state&);
void log_lua_initialise(lua_state&);



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

    if (g_force_load_debugger || g_lua_debug.get())
        lua_load_script(self, lib, debugger);

    lua_load_script(self, lib, core);

    clink_lua_initialise(self);
    os_lua_initialise(self);
    path_lua_initialise(self);
    settings_lua_initialise(self);
    string_lua_initialise(self);
    log_lua_initialise(self);
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
        ok = !pcall(0, LUA_MULTRET);

    lua_settop(m_state, 0);
    return ok;
}

//------------------------------------------------------------------------------
bool lua_state::do_file(const char* path)
{
    bool ok;
    if (ok = !luaL_loadfile(m_state, path))
        ok = !pcall(0, LUA_MULTRET);

    lua_settop(m_state, 0);
    return ok;
}

//------------------------------------------------------------------------------
int lua_state::pcall(lua_State* L, int nargs, int nresults)
{
    // Calculate stack position for message handler.
    int hpos = lua_gettop(L) - nargs;

    // Push our error message handler.
    lua_getglobal(L, "_error_handler");

    // Move it before function and arguments.
    lua_insert(L, hpos);

    // Call lua_pcall with custom handler.
    int ret = lua_pcall(L, nargs, nresults, hpos);

    // Remove custom error message handler from stack.
    lua_remove(L, hpos);

    return ret;
}
