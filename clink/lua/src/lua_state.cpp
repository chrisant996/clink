// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_script_loader.h"
#include "rl_buffer_lua.h"

#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <core/os.h>

#include <assert.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

class line_buffer;
extern line_buffer* g_rl_buffer;

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
void console_lua_initialise(lua_state&);
void path_lua_initialise(lua_state&);
void rl_lua_initialise(lua_state&);
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
        lua_pushliteral(m_state, "path");
        lua_pushlstring(m_state, path.c_str(), path.length());
        lua_rawset(m_state, -3);
    }

    lua_state& self = *this;

    // Initialize API namespaces.
    clink_lua_initialise(self);
    os_lua_initialise(self);
    console_lua_initialise(self);
    path_lua_initialise(self);
    rl_lua_initialise(self);
    settings_lua_initialise(self);
    string_lua_initialise(self);
    log_lua_initialise(self);

    // Load the debugger.
    if (g_force_load_debugger || g_lua_debug.get())
        lua_load_script(self, lib, debugger);

    // Load core scripts.
    lua_load_script(self, lib, core);
    lua_load_script(self, lib, events);
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

    bool ok = !luaL_loadbuffer(m_state, string, length, string);
    if (ok)
        ok = !pcall(0, LUA_MULTRET);
    else if (const char* error = lua_tostring(m_state, -1))
        print_error(error);

    lua_settop(m_state, 0);
    return ok;
}

//------------------------------------------------------------------------------
bool lua_state::do_file(const char* path)
{
    bool ok = !luaL_loadfile(m_state, path);
    if (ok)
        ok = !pcall(0, LUA_MULTRET);
    else if (const char* error = lua_tostring(m_state, -1))
        print_error(error);

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

//------------------------------------------------------------------------------
const char* lua_state::get_string(int index) const
{
    if (lua_gettop(m_state) < index || !lua_isstring(m_state, index))
        return nullptr;

    return lua_tostring(m_state, index);
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void dump_lua_stack(lua_State* state, int pos)
{
    static const char *const lua_type_names[] =
    {
        "LUA_TNONE",
        "LUA_TNIL",
        "LUA_TBOOLEAN",
        "LUA_TLIGHTUSERDATA",
        "LUA_TNUMBER",
        "LUA_TSTRING",
        "LUA_TTABLE",
        "LUA_TFUNCTION",
        "LUA_TUSERDATA",
        "LUA_TTHREAD",
    };

    int top = lua_gettop(state);
    if (pos >= 0)
        pos -= top;

    printf("LUA_STACK from %d to %d:\n", top + pos, top);
    while (pos < 0)
    {
        int type = lua_type(state, pos);
        const char* type_name = lua_type_names[type + 1];

        printf("[%d] type %s ", pos, type_name);
        switch (type)
        {
        case LUA_TNIL:
            puts("nil");
            break;
        case LUA_TBOOLEAN:
            puts(lua_toboolean(state, pos) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            {
                LUA_NUMBER tmp = lua_tonumber(state, pos);
                printf("%f", tmp);
            }
            break;
        case LUA_TSTRING:
            {
                const char* tmp = lua_tostring(state, pos);
                if (tmp)
                    printf("\"%s\"\n", tmp);
                else
                    puts("nil");
            }
            break;
        default:
            puts("");
            break;
        }

        pos++;
    }
}
#endif

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.  Arguments can be
// passed by passing nargs equal to the number of pushed arguments.  On success,
// the stack is left with nret return values.  On error, the stack is popped to
// the original level.
bool lua_state::send_event_internal(const char* event_name, const char* event_mechanism, int nargs, int nret)
{
    bool ret = false;
    lua_State* state = get_state();

    int top = lua_gettop(state);
    int pos = top - nargs;
    assert(pos >= 0);

    // Push the global _send_event function.
    lua_getglobal(state, "clink");
    lua_pushstring(state, event_mechanism);
    lua_rawget(state, -2);
    if (lua_isnil(state, -1))
        goto done;
    lua_insert(state, -2);
    lua_pop(state, 1);

    // Push the event name.
    lua_pushstring(state, event_name);

    // Move event name and mechanism (e.g. "_send_event") before nargs.
    if (pos < top)
    {
        int ins = pos - lua_gettop(state);
        lua_insert(state, ins);
        lua_insert(state, ins);
    }

    // Call the event callback.
    if (pcall(1 + nargs, nret) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            print_error(error);
        goto done;
    }

    ret = true;

done:
    if (ret)
    {
        int crem = lua_gettop(state) - pos - nret;
        int irem = pos - lua_gettop(state);
        while (crem-- > 0)
            lua_remove(state, irem);
    }
    else
    {
        lua_settop(state, pos);
    }
    return ret;
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event(const char* event_name, int nargs)
{
    return send_event_internal(event_name, "_send_event", nargs);
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_cancelable(const char* event_name, int nargs)
{
    return send_event_internal(event_name, "_send_event_cancelable", nargs);
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out)
{
    if (!string)
        string = "";

    lua_pushstring(m_state, string);

    if (!send_event_internal(event_name, "_send_event_cancelable_string_inout", 1, 1))
        return false;

    const char* result = get_string(-1);
    if (result)
        out = result;

    return true;
}

//------------------------------------------------------------------------------
bool lua_state::call_lua_rl_global_function(const char* func_name)
{
    lua_State* state = get_state();

    bool first = true;
    str_iter part;
    str_tokeniser name_parts(func_name, ".");
    while (name_parts.next(part))
    {
        if (first)
        {
            str<16> global;
            global.concat(part.get_pointer(), part.length());
            lua_getglobal(state, global.c_str());
            first = false;
        }
        else
        {
            lua_pushlstring(state, part.get_pointer(), part.length());
            lua_rawget(state, -2);
        }
    }

    if (!lua_isfunction(state, -1))
        return false;

    rl_buffer_lua buffer(*g_rl_buffer);
    buffer.push(state);

    if (pcall(1, 0) != LUA_OK)
    {
        if (const char* error = lua_tostring(state, -1))
        {
            printf("\nerror executing function '%s':\n", func_name);
            puts(error);
        }
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
void lua_state::print_error(const char* error)
{
    puts("");
    puts(error);
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void lua_state::dump_stack(int pos)
{
    dump_lua_stack(get_state(), pos);
}
#endif
