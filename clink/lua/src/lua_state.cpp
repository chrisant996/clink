// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_script_loader.h"
#include "rl_buffer_lua.h"
#include "line_state_lua.h"

#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <core/os.h>
#include <core/debugheap.h>
#include <lib/cmd_tokenisers.h>

#include <memory>
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
    "as a breakpoint.  Or the debugger can be activated by traceback() calls or\n"
    "Lua errors by turning on the lua.break_on_traceback or lua.break_on_error\n"
    "settings, respectively.",
    false);

static setting_str g_lua_path(
    "lua.path",
    "'require' search path",
    "Value to append to package.path.  Used to search for Lua scripts specified\n"
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

setting_bool g_lua_strict(
    "lua.strict",
    "Fail on argument errors",
    "When enabled, argument errors cause Lua scripts to fail.  This may expose\n"
    "bugs in some older scripts, causing them to fail where they used to succeed.\n"
    "In that case you can try turning this off, but please alert the script owner\n"
    "about the issue so they can fix the script.",
    true);



//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state&, bool lua_interpreter=false);
void os_lua_initialise(lua_state&);
void io_lua_initialise(lua_state&);
void console_lua_initialise(lua_state&);
void path_lua_initialise(lua_state&);
void rl_lua_initialise(lua_state&);
void settings_lua_initialise(lua_state&);
void string_lua_initialise(lua_state&);
void unicode_lua_initialise(lua_state&);
void log_lua_initialise(lua_state&);



//------------------------------------------------------------------------------
int checkinteger(lua_State* state, int index, bool* pisnum)
{
    int isnum;
    lua_Integer d = lua_tointegerx(state, index, &isnum);
    if (pisnum)
        *pisnum = !!isnum;
    if (isnum)
        return int(d);

    if (g_lua_strict.get())
        return int(luaL_checkinteger(state, index));

    return 0;
}

//------------------------------------------------------------------------------
int optinteger(lua_State* state, int index, int default_value, bool* pisnum)
{
    if (lua_isnoneornil(state, index))
    {
        if (pisnum)
            *pisnum = true;
        return default_value;
    }

    return checkinteger(state, index, pisnum);
}

//------------------------------------------------------------------------------
lua_Number checknumber(lua_State* state, int index, bool* pisnum)
{
    int isnum;
    lua_Number d = lua_tonumberx(state, index, &isnum);
    if (pisnum)
        *pisnum = !!isnum;
    if (isnum)
        return d;

    if (g_lua_strict.get())
        return luaL_checknumber(state, index);

    return 0;
}

//------------------------------------------------------------------------------
lua_Number optnumber(lua_State* state, int index, lua_Number default_value, bool* pisnum)
{
    if (lua_isnoneornil(state, index))
    {
        if (pisnum)
            *pisnum = true;
        return default_value;
    }

    return checknumber(state, index, pisnum);
}

//------------------------------------------------------------------------------
const char* checkstring(lua_State* state, int index)
{
    if (g_lua_strict.get())
        return luaL_checkstring(state, index);

    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
const char* optstring(lua_State* state, int index, const char* default_value)
{
    if (g_lua_strict.get())
        return luaL_optstring(state, index, default_value);

    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return default_value;

    return lua_tostring(state, index);
}



//------------------------------------------------------------------------------
bool lua_state::s_in_luafunc = false;
bool lua_state::s_interpreter = false;

//------------------------------------------------------------------------------
lua_state::lua_state(lua_state_flags flags)
: m_state(nullptr)
{
    initialise(flags);
}

//------------------------------------------------------------------------------
lua_state::~lua_state()
{
    shutdown();
}

//------------------------------------------------------------------------------
void lua_state::initialise(lua_state_flags flags)
{
    shutdown();

    const bool interpreter = !!int(flags & lua_state_flags::interpreter);
    const bool no_env = !!int(flags & lua_state_flags::no_env);

    s_interpreter = interpreter;

    // Create a new Lua state.
    m_state = luaL_newstate();

    // Suspend collection during initialization.
    lua_gc(m_state, LUA_GCSTOP, 0);

    if (no_env)
    {
        // Signal for libraries to ignore env. vars.
        lua_pushboolean(m_state, 1);
        lua_setfield(m_state, LUA_REGISTRYINDEX, "LUA_NOENV");
    }

    // Open the standard Lua libraries.
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
    clink_lua_initialise(self, interpreter);
    os_lua_initialise(self);
    io_lua_initialise(self);
    console_lua_initialise(self);
    path_lua_initialise(self);
    if (!interpreter)
        rl_lua_initialise(self);
    settings_lua_initialise(self);
    string_lua_initialise(self);
    unicode_lua_initialise(self);
    log_lua_initialise(self);

    // Load the debugger.
    if (g_force_load_debugger || g_lua_debug.get())
        lua_load_script(self, lib, debugger);

    // Load core scripts.
    lua_load_script(self, lib, core);
    lua_load_script(self, lib, console);
    if (!interpreter)
    {
        lua_load_script(self, lib, events);
        lua_load_script(self, lib, coroutines);
    }

    // Load match generator scripts.
    if (!interpreter)
    {
        lua_load_script(self, lib, generator);
        lua_load_script(self, lib, classifier);
        lua_load_script(self, lib, arguments);
    }

    lua_gc(m_state, LUA_GCRESTART, 0);  // Resume collection.
}

//------------------------------------------------------------------------------
void lua_state::shutdown()
{
    if (m_state == nullptr)
        return;

    lua_close(m_state);
    m_state = nullptr;

    s_interpreter = false;
}

//------------------------------------------------------------------------------
bool lua_state::do_string(const char* string, int length)
{
    save_stack_top ss(m_state);

    if (length < 0)
        length = int(strlen(string));

    int err = luaL_loadbuffer(m_state, string, length, string);
    if (err)
    {
        if (g_lua_debug.get())
        {
            if (const char* error = lua_tostring(m_state, -1))
                puts(error);
        }
        return false;
    }

    err = pcall(0, LUA_MULTRET);
    if (err)
        return false;

    return true;
}

//------------------------------------------------------------------------------
bool lua_state::do_file(const char* path)
{
    save_stack_top ss(m_state);

    int err = luaL_loadfile(m_state, path);
    if (err)
    {
        if (g_lua_debug.get())
        {
            if (const char* error = lua_tostring(m_state, -1))
                puts(error);
        }
        return false;
    }

    err = pcall(0, LUA_MULTRET);
    if (err)
        return false;

    return true;
}

//------------------------------------------------------------------------------
bool lua_state::push_named_function(lua_State* state, const char* func_name, str_base* e)
{
    bool first = true;
    str_iter part;
    str_tokeniser name_parts(func_name, ".");
    str<> global;
    const char* error = nullptr;
    while (name_parts.next(part))
    {
        if (first)
        {
            global.concat(part.get_pointer(), part.length());
            lua_getglobal(state, global.c_str());
            first = false;
        }
        else
        {
            if (lua_isnil(state, -1))
            {
                error = "'%s' is nil";
report_error:
                if (e)
                {
                    e->format("can't execute '%s'", func_name);
                    if (error)
                    {
                        str<> tmp;
                        tmp.format(error, global.c_str());
                        (*e) << "; " << tmp;
                    }
                    (*e) << "\n";
                }
                return false;
            }
            else if (!lua_istable(state, -1))
            {
                error = "'%s' is not a table";
                goto report_error;
            }

            lua_pushlstring(state, part.get_pointer(), part.length());
            lua_rawget(state, -2);

            global.clear();
            global.concat(func_name, int(part.get_pointer() - func_name + part.length()));
        }
    }

    if (first || !lua_isfunction(state, -1))
    {
        lua_pop(state, 1);
        error = "not a function";
        goto report_error;
    }

    if (e)
        e->clear();
    return true;
}

//------------------------------------------------------------------------------
int lua_state::pcall_silent(lua_State* L, int nargs, int nresults)
{
    // Lua scripts can have a side effect of changing the console mode (e.g. if
    // they spawn another process, such as in a prompt filter), so always
    // save/restore the console mode around Lua pcalls.
    DWORD modeIn, modeOut;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hIn, &modeIn);
    GetConsoleMode(hOut, &modeOut);

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

    // Restore the console mode.
    SetConsoleMode(hIn, modeIn);
    SetConsoleMode(hOut, modeOut);
    return ret;
}

//------------------------------------------------------------------------------
int lua_state::pcall(lua_State* L, int nargs, int nresults)
{
    const int ret = pcall_silent(L, nargs, nresults);
    if (ret != 0)
    {
        if (const char* error = lua_tostring(L, -1))
        {
            puts("");
            puts(error);
        }
    }
    return ret;
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
// the original level and then nargs are popped.
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
    {
        lua_settop(state, top);
        lua_pop(state, nargs);
        return false;
    }
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

    // Preserve cwd around events.
    os::cwd_restorer cwd;

    // Call the event callback.
    return pcall(1 + nargs, nret) == 0;
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event(const char* event_name, int nargs)
{
    return send_event_internal(event_name, "_send_event", nargs);
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_string_out(const char* event_name, str_base& out, int nargs)
{
    if (!send_event_internal(event_name, "_send_event_string_out", nargs, 1))
        return false;

    if (lua_isstring(m_state, -1))
        out = lua_tostring(m_state, -1);

    return true;
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_cancelable(const char* event_name, int nargs)
{
    return send_event_internal(event_name, "_send_event_cancelable", nargs);
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out, std::list<str_moveable>* more_out)
{
    if (!string)
        string = "";

    lua_pushstring(m_state, string);

    if (!send_event_internal(event_name, "_send_event_cancelable_string_inout", 1, 1))
        return false;

    if (lua_isstring(m_state, -1))
    {
        out = lua_tostring(m_state, -1);
    }
    else if (lua_istable(m_state, -1))
    {
        out.clear();

        const size_t len = lua_rawlen(m_state, -1);
        for (unsigned int i = 1; i <= len; ++i)
        {
            lua_rawgeti(m_state, -1, i);
            if (lua_isstring(m_state, -1))
            {
                const char* s = lua_tostring(m_state, -1);
                if (i == 1)
                    out = s;
                else if (more_out)
                    more_out->emplace_back(s);
                lua_pop(m_state, 1);
            }
        }
    }

    return true;
}

//------------------------------------------------------------------------------
bool lua_state::send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file)
{
    assert(recog != recognition::unknown);
    if (recog != recognition::unrecognized && recog != recognition::executable)
        return false;

    lua_State* state = get_state();
    line_state_lua line_lua(line);

    const char* type;
    if (!quoted && is_cmd_command(command))
        type = "command";
    else if (recog == recognition::executable)
        type = "executable";
    else
        type = "unrecognized";

    line_lua.push(state);

    lua_createtable(state, 0, 4);

    lua_pushliteral(state, "command");
    lua_pushstring(state, command);
    lua_rawset(state, -3);

    lua_pushliteral(state, "quoted");
    lua_pushboolean(state, quoted);
    lua_rawset(state, -3);

    lua_pushliteral(state, "type");
    lua_pushstring(state, type);
    lua_rawset(state, -3);

    lua_pushliteral(state, "file");
    lua_pushstring(state, file);
    lua_rawset(state, -3);

    return send_event("oncommand", 2);
}

//------------------------------------------------------------------------------
bool lua_state::call_lua_rl_global_function(const char* func_name, line_state* line)
{
    lua_State* state = get_state();
    rl_buffer_lua buffer(*g_rl_buffer);
    std::unique_ptr<line_state_lua> line_lua = std::unique_ptr<line_state_lua>(line ? new line_state_lua(*line) : nullptr);

    str<> msg;
    if (!push_named_function(state, func_name, &msg))
    {
        buffer.begin_output(state);
        puts(msg.c_str());
        return false;
    }

    extern void override_rl_last_func(rl_command_func_t* func, bool force_when_null=false);
    override_rl_last_func(nullptr);

    buffer.push(state);
    if (line_lua)
        line_lua->push(state);
    else
        lua_pushnil(state);

    rollback<bool> rb(s_in_luafunc, true);
    bool success = (pcall_silent(2, 0) == LUA_OK);

    extern void set_pending_luafunc(const char *);
    set_pending_luafunc(func_name);

    if (!success)
    {
        if (const char* error = lua_tostring(state, -1))
        {
            buffer.begin_output(state);
            printf("error executing function '%s':\n", func_name);
            puts(error);
        }
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void lua_state::dump_stack(int pos)
{
    dump_lua_stack(get_state(), pos);
}
#endif



//------------------------------------------------------------------------------
save_stack_top::save_stack_top(lua_State* L)
: m_state(L)
, m_top(lua_gettop(L))
{
}

//------------------------------------------------------------------------------
save_stack_top::~save_stack_top()
{
    assert(lua_gettop(m_state) >= m_top);
    lua_settop(m_state, m_top);
}



//------------------------------------------------------------------------------
void get_lua_srcinfo(lua_State* L, str_base& out)
{
    lua_Debug ar = {};
    lua_getstack(L, 1, &ar);
    lua_getinfo(L, "Sl", &ar);
    const char* source = ar.source ? ar.source : "?";

    out.clear();
    out.format("%s:%d", source, ar.currentline);
}



//------------------------------------------------------------------------------
#ifdef USE_MEMORY_TRACKING
extern "C" DECLALLOCATOR DECLRESTRICT void* __cdecl dbgluarealloc(void* pv, size_t size)
{
    pv = dbgrealloc_(pv, size, 0|memSkipOneFrame|memIgnoreLeak);
    if (pv)
    {
        dbgsetignore(pv, true);
        dbgsetlabel(pv, "LUA alloc", false);
    }
    return pv;
}
#endif
