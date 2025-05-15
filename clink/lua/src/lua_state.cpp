// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_script_loader.h"
#include "lua_task_manager.h"
#include "rl_buffer_lua.h"
#include "line_state_lua.h"

#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <core/os.h>
#include <core/debugheap.h>
#include <core/callstack.h>
#include <core/log.h>
#include <lib/cmd_tokenisers.h>
#include <lib/recognizer.h>
#include <lib/line_editor_integration.h>
#include <lib/rl_integration.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>

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

setting_bool g_lua_breakonerror(
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

static setting_int g_lua_throttle_coroutines(
    "lua.throttle_interval",
    "Restricts coroutine execution",
    "This is off (0) by default, which allows coroutines to freely control\n"
    "their own execution times and rates.  If coroutines interfere with\n"
    "responsiveness, you can set this to a number that restricts how often (in\n"
    "seconds) a long-running coroutine can actually run.  Until v1.7.17, the\n"
    "default value was 5 seconds, but now it's 0 (no throttling).",
    0);

extern setting_bool g_debug_log_terminal;
#ifdef _MSC_VER
extern setting_bool g_debug_log_output_callstacks;
#endif



//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state&, bool lua_interpreter=false);
void os_lua_initialise(lua_state&);
void io_lua_initialise(lua_state&);
void console_lua_initialise(lua_state&, bool lua_interpreter=false);
void path_lua_initialise(lua_state&);
void rl_lua_initialise(lua_state&, bool lua_interpreter=false);
void settings_lua_initialise(lua_state&);
void string_lua_initialise(lua_state&);
void unicode_lua_initialise(lua_state&);
void log_lua_initialise(lua_state&);



//------------------------------------------------------------------------------
checked_num<int32> checkinteger(lua_State* L, int32 index)
{
    int32 isnum;
    lua_Integer d = lua_tointegerx(L, index, &isnum);
    if (!isnum && g_lua_strict.get())
        d = luaL_checkinteger(L, index);
    return checked_num<int32>(int32(d), !!isnum);
}

//------------------------------------------------------------------------------
checked_num<int32> optinteger(lua_State* L, int32 index, int32 default_value)
{
    if (lua_isnoneornil(L, index))
        return checked_num<int32>(default_value);

    return checkinteger(L, index);
}

//------------------------------------------------------------------------------
checked_num<lua_Number> checknumber(lua_State* L, int32 index)
{
    int32 isnum;
    lua_Number d = lua_tonumberx(L, index, &isnum);
    if (!isnum && g_lua_strict.get())
        d = luaL_checknumber(L, index);
    return checked_num<lua_Number>(d, !!isnum);
}

//------------------------------------------------------------------------------
checked_num<lua_Number> optnumber(lua_State* L, int32 index, lua_Number default_value)
{
    if (lua_isnoneornil(L, index))
        return checked_num<lua_Number>(default_value);

    return checknumber(L, index);
}

//------------------------------------------------------------------------------
const char* checkstring(lua_State* L, int32 index)
{
    if (g_lua_strict.get())
        return luaL_checkstring(L, index);

    if (lua_gettop(L) < index || !lua_isstring(L, index))
        return nullptr;

    return lua_tostring(L, index);
}

//------------------------------------------------------------------------------
const char* optstring(lua_State* L, int32 index, const char* default_value)
{
    if (g_lua_strict.get())
        return luaL_optstring(L, index, default_value);

    if (lua_gettop(L) < index || !lua_isstring(L, index))
        return default_value;

    return lua_tostring(L, index);
}



//------------------------------------------------------------------------------
enum class global_state : uint32
{
    none                = 0x00,
    in_luafunc          = 0x01,
    in_onfiltermatches  = 0x02,
#ifdef DEBUG
    in_coroutine        = 0x80,
#endif
};
DEFINE_ENUM_FLAG_OPERATORS(global_state);

//------------------------------------------------------------------------------
bool lua_state::s_internal = false;
bool lua_state::s_interpreter = false;
bool lua_state::s_in_luafunc = false;
bool lua_state::s_in_onfiltermatches = false;
#ifdef DEBUG
bool lua_state::s_in_coroutine = false;
#endif

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
    assert(!s_internal);

    shutdown();

    const bool interpreter = !!int32(flags & lua_state_flags::interpreter);
    const bool no_env = !!int32(flags & lua_state_flags::no_env);

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
    console_lua_initialise(self, interpreter);
    path_lua_initialise(self);
    rl_lua_initialise(self, interpreter);
    settings_lua_initialise(self);
    string_lua_initialise(self);
    unicode_lua_initialise(self);
    log_lua_initialise(self);

    // Load the debugger.
    if (g_force_load_debugger || g_lua_debug.get())
        lua_load_script(self, lib, debugger);

    // Load core scripts.
    lua_load_script(self, lib, error);
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
        lua_load_script(self, lib, hinter);
        lua_load_script(self, lib, arguments);
    }

    lua_gc(m_state, LUA_GCRESTART, 0);  // Resume collection.
}

//------------------------------------------------------------------------------
void lua_state::shutdown()
{
    if (m_state == nullptr)
        return;

    shutdown_task_manager(false/*final*/);

    lua_close(m_state);
    m_state = nullptr;

    s_interpreter = false;
}

//------------------------------------------------------------------------------
#ifdef DEBUG
lua_State* lua_state::get_state() const
{
    assert(!s_in_coroutine);
    return m_state;
}
#endif

//------------------------------------------------------------------------------
bool lua_state::do_string(const char* string, int32 length, str_base* error, const char* name)
{
    lua_State* L = get_state();

    save_stack_top ss(L);

    if (length < 0)
        length = int32(strlen(string));

    int32 err = luaL_loadbuffer(L, string, length, name ? name : string);
    if (err)
    {
        if (error)
            *error = lua_tostring(L, -1);
        else if (g_lua_debug.get())
        {
            if (const char* errmsg = lua_tostring(L, -1))
            {
                puts("");
                puts(errmsg);
            }
        }
        return false;
    }

    err = pcall(L, 0, LUA_MULTRET, error);
    if (err)
        return false;

    return true;
}

//------------------------------------------------------------------------------
bool lua_state::do_file(const char* path)
{
    lua_State* L = get_state();

    save_stack_top ss(L);

    int32 err = luaL_loadfile(L, path);
    if (err)
    {
        if (g_lua_debug.get())
        {
            if (const char* errmsg = lua_tostring(L, -1))
                puts(errmsg);
        }
        return false;
    }

    err = pcall(L, 0, LUA_MULTRET);
    if (err)
        return false;

    return true;
}

//------------------------------------------------------------------------------
bool lua_state::push_named_function(lua_State* L, const char* func_name, str_base* e)
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
            lua_getglobal(L, global.c_str());
            first = false;
        }
        else
        {
            if (lua_isnil(L, -1))
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
            else if (!lua_istable(L, -1))
            {
                error = "'%s' is not a table";
                goto report_error;
            }

            lua_pushlstring(L, part.get_pointer(), part.length());
            lua_rawget(L, -2);

            global.clear();
            global.concat(func_name, int32(part.get_pointer() - func_name + part.length()));
        }
    }

    if (first || !lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        error = "not a function";
        goto report_error;
    }

    if (e)
        e->clear();
    return true;
}

//------------------------------------------------------------------------------
int32 lua_state::pcall_silent(lua_State* L, int32 nargs, int32 nresults)
{
    // Lua scripts can have a side effect of changing the console mode (e.g. if
    // they spawn another process, such as in a prompt filter), so always
    // save/restore the console mode around Lua pcalls.
    DWORD modeIn, modeOut;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hIn, &modeIn);
    GetConsoleMode(hOut, &modeOut);
    modeIn = cleanup_console_input_mode(modeIn);

    // Calculate stack position for message handler.
    int32 hpos = lua_gettop(L) - nargs;

    // Push our error message handler.
    lua_getglobal(L, "_error_handler");

    // Move it before function and arguments.
    lua_insert(L, hpos);

    // Call lua_pcall with custom handler.
    int32 ret = lua_pcall(L, nargs, nresults, hpos);

    // Remove custom error message handler from stack.
    lua_remove(L, hpos);

    // Restore the console mode.
    DWORD afterIn, afterOut;
    GetConsoleMode(hIn, &afterIn);
    GetConsoleMode(hOut, &afterOut);
    if (modeOut != afterOut)
        SetConsoleMode(hOut, modeOut);
    if (modeIn != afterIn)
    {
        SetConsoleMode(hIn, modeIn);
        debug_show_console_mode(nullptr, "Lua pcall");
    }
    return ret;
}

//------------------------------------------------------------------------------
int32 lua_state::pcall(lua_State* L, int32 nargs, int32 nresults, str_base* error)
{
    const int32 ret = pcall_silent(L, nargs, nresults);
    if (ret != 0)
    {
        const char* errmsg = lua_tostring(L, -1);
        if (error)
            *error = errmsg;
        else if (errmsg)
        {
            puts("");
            puts(errmsg);
        }
    }
    return ret;
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void dump_lua_stack(lua_State* L, int32 pos)
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

    int32 top = lua_gettop(L);
    if (pos >= 0)
        pos -= top;

    printf("LUA_STACK from %d to %d:\n", top + pos, top);
    while (pos < 0)
    {
        int32 type = lua_type(L, pos);
        const char* type_name = lua_type_names[type + 1];

        printf("[%d] type %s ", pos, type_name);
        switch (type)
        {
        case LUA_TNIL:
            puts("nil");
            break;
        case LUA_TBOOLEAN:
            puts(lua_toboolean(L, pos) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            {
                LUA_NUMBER tmp = lua_tonumber(L, pos);
                printf("%f\n", tmp);
            }
            break;
        case LUA_TSTRING:
            {
                const char* tmp = lua_tostring(L, pos);
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
void lua_state::activate_clinkprompt_module(lua_State* L, const char* module)
{
#ifdef DEBUG
    const int32 top = lua_gettop(L);
#endif

    lua_getglobal(L, "clink");
    lua_pushliteral(L, "_activate_clinkprompt_module");
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1))
    {
        if (module)
            lua_pushstring(L, module);
        pcall(L, module ? 1 : 0, 1);
        const char* err = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
        if (err)
        {
            assert(*err);
            puts(err);
        }
    }
    lua_pop(L, 2);

    assert(lua_gettop(L) == top);
}

//------------------------------------------------------------------------------
void lua_state::load_colortheme_in_memory(lua_State* L, const char* theme)
{
    if (!theme || !*theme)
        return;

#ifdef DEBUG
    const int32 top = lua_gettop(L);
#endif

    lua_getglobal(L, "clink");
    lua_pushliteral(L, "_load_colortheme_in_memory");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
    }
    else
    {
        lua_pushstring(L, theme);
        pcall(L, 1, 0);
    }
    lua_pop(L, 1);

    assert(lua_gettop(L) == top);
}

//------------------------------------------------------------------------------
void lua_state::activate_clinkprompt_module(const char* module)
{
    activate_clinkprompt_module(get_state(), module);
}

//------------------------------------------------------------------------------
void lua_state::load_colortheme_in_memory(const char* theme)
{
    load_colortheme_in_memory(get_state(), theme);
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.  Arguments can be
// passed by passing nargs equal to the number of pushed arguments.  On success,
// the stack is left with nret return values.  On error, the stack is popped to
// the original level and then nargs are popped.
bool lua_state::send_event_internal(lua_State* L, const char* event_name, const char* event_mechanism, int32 nargs, int32 nret)
{
    bool ret = false;

    int32 top = lua_gettop(L);
    int32 pos = top - nargs;
    assert(pos >= 0);

    // Push the global _send_event function.
    lua_getglobal(L, "clink");
    lua_pushstring(L, event_mechanism);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1))
    {
        lua_settop(L, top);
        lua_pop(L, nargs);
        return false;
    }
    lua_insert(L, -2);
    lua_pop(L, 1);

    // Push the event name.
    lua_pushstring(L, event_name);

    // Move event name and mechanism (e.g. "_send_event") before nargs.
    if (pos < top)
    {
        int32 ins = pos - lua_gettop(L);
        lua_insert(L, ins);
        lua_insert(L, ins);
    }

    // Preserve cwd around events.
    os::cwd_restorer cwd;

    // Call the event callback.
    return pcall(L, 1 + nargs, nret) == 0;
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event(lua_State* L, const char* event_name, int32 nargs)
{
    return send_event_internal(L, event_name, "_send_event", nargs);
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event(const char* event_name, int32 nargs)
{
    return send_event(get_state(), event_name, nargs);
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_string_out(const char* event_name, str_base& out, int32 nargs)
{
    lua_State* L = get_state();

    if (!send_event_internal(L, event_name, "_send_event_string_out", nargs, 1))
        return false;

    if (lua_isstring(L, -1))
        out = lua_tostring(L, -1);

    return true;
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_cancelable(const char* event_name, int32 nargs)
{
    lua_State* L = get_state();

    if (!send_event_internal(L, event_name, "_send_event_cancelable", nargs, 1))
        return false;

    if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
        return true;

    return false;
}

//------------------------------------------------------------------------------
// Calls any event_name callbacks registered by scripts.
bool lua_state::send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out, std::list<str_moveable>* more_out)
{
    lua_State* L = get_state();

    if (!string)
        string = "";

    lua_pushstring(L, string);

    if (!send_event_internal(L, event_name, "_send_event_cancelable_string_inout", 1, 1))
        return false;

    if (lua_isstring(L, -1))
    {
        out = lua_tostring(L, -1);
    }
    else if (lua_istable(L, -1))
    {
        out.clear();

        const size_t len = lua_rawlen(L, -1);
        for (uint32 i = 1; i <= len; ++i)
        {
            lua_rawgeti(L, -1, i);
            if (lua_isstring(L, -1))
            {
                const char* s = lua_tostring(L, -1);
                if (i == 1)
                    out = s;
                else if (more_out)
                    more_out->emplace_back(s);
                lua_pop(L, 1);
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

    lua_State* L = get_state();
    line_state_lua line_lua(line);

    const char* type;
    if (!quoted && !!is_cmd_command(command))
        type = "command";
    else if (recog == recognition::executable)
        type = "executable";
    else
        type = "unrecognized";

    line_lua.push(L);

    lua_createtable(L, 0, 4);

    lua_pushliteral(L, "command");
    lua_pushstring(L, command);
    lua_rawset(L, -3);

    lua_pushliteral(L, "quoted");
    lua_pushboolean(L, quoted);
    lua_rawset(L, -3);

    lua_pushliteral(L, "type");
    lua_pushstring(L, type);
    lua_rawset(L, -3);

    lua_pushliteral(L, "file");
    lua_pushstring(L, file);
    lua_rawset(L, -3);

    return send_event(L, "oncommand", 2);
}

//------------------------------------------------------------------------------
bool lua_state::send_oninputlinechanged_event(const char* line)
{
    lua_State* L = get_state();
    lua_pushstring(L, line);
    return send_event(L, "oninputlinechanged", 1);
}

//------------------------------------------------------------------------------
bool lua_state::get_command_word(line_state& line, str_base& command_word, bool& quoted, recognition& recog, str_base& file)
{
    lua_State* L = get_state();
    save_stack_top ss(L);

    command_word.clear();
    quoted = false;
    recog = recognition::unknown;
    file.clear();

    // Call to Lua to calculate prefix length.
    lua_getglobal(L, "clink");
    lua_pushliteral(L, "_get_command_word");
    lua_rawget(L, -2);

    line_state_lua line_lua(line);
    line_lua.push(L);

    os::cwd_restorer cwd;

    if (lua_state::pcall(L, 1, 4) != 0)
        return false;

    if (lua_isstring(L, -4))
        command_word = lua_tostring(L, -4);

    quoted = !!lua_toboolean(L, -3);

    if (lua_isstring(L, -2))
    {
        const char* r = lua_tostring(L, -2);
        if (r)
        {
            switch (*r)
            {
            case 'x':   recog = recognition::executable; break;
            case 'u':   recog = recognition::unrecognized; break;
            }
        }

        const char* f = lua_tostring(L, -1);
        file = f;
    }

    return !command_word.empty();
}

//------------------------------------------------------------------------------
bool lua_state::call_lua_rl_global_function(const char* func_name, const line_state* line)
{
    lua_State* L = get_state();
    rl_buffer_lua buffer(*g_rl_buffer);
    std::unique_ptr<line_state_lua> line_lua = std::unique_ptr<line_state_lua>(line ? new line_state_lua(*line) : nullptr);

    str<> msg;
    if (!push_named_function(L, func_name, &msg))
    {
        buffer.do_begin_output();
        puts(msg.c_str());
        return false;
    }

    override_rl_last_func(nullptr);

    buffer.push(L);
    if (line_lua)
        line_lua->push(L);
    else
        lua_pushnil(L);

    rollback<bool> rb(s_in_luafunc, true);
    bool success = (pcall_silent(L, 2, 0) == LUA_OK);

    set_pending_luafunc(func_name);

    if (!success)
    {
        if (const char* error = lua_tostring(L, -1))
        {
            buffer.do_begin_output();
            printf("error executing function '%s':\n", func_name);
            puts(error);
        }
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
int32 lua_state::call_onfiltermatches(lua_State* L, int32 nargs, int32 nresults)
{
    rollback<bool> rb(s_in_onfiltermatches, true);
    return pcall(L, nargs, nresults);
}

//------------------------------------------------------------------------------
uint32 lua_state::save_global_states(bool new_coroutine)
{
    global_state states = global_state::none;
    if (new_coroutine)
    {
#ifdef DEBUG
        states |= global_state::in_coroutine;
#endif
    }
    else
    {
        if (is_in_luafunc())            states |= global_state::in_luafunc;
        if (is_in_onfiltermatches())    states |= global_state::in_onfiltermatches;
#ifdef DEBUG
        if (s_in_coroutine)             states |= global_state::in_coroutine;
#endif
    }
    return uint32(states);
}

//------------------------------------------------------------------------------
void lua_state::restore_global_states(uint32 _states)
{
    global_state states = global_state(_states);
    s_in_luafunc = (states & global_state::in_luafunc) != global_state::none;
    s_in_onfiltermatches = (states & global_state::in_onfiltermatches) != global_state::none;
#ifdef DEBUG
    s_in_coroutine = (states & global_state::in_coroutine) != global_state::none;
#endif
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void lua_state::dump_stack(int32 pos)
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
static terminal_in* s_lua_term_in = nullptr;
static terminal_out* s_lua_term_out = nullptr;
void set_lua_terminal(terminal_in* in, terminal_out* out)
{
    s_lua_term_in = in;
    s_lua_term_out = out;
}
terminal_in* get_lua_terminal_input() { return s_lua_term_in; }
terminal_out* get_lua_terminal_output() { return s_lua_term_out; }

//------------------------------------------------------------------------------
bool g_direct_lua_fwrite = false;
extern "C" void lua_fwrite(void const* buffer, size_t size, size_t count, FILE* stream)
{
    // The C runtime implementation of fwrite has two undesirable
    // characteristics when printing a UTF8 string:  it performs one
    // WriteConsoleW call PER CHARACTER and it cannot print non-ASCII
    // codepoints (nor can printf or fprintf).
    //
    // This replacement for fwrite adds optional logging and converts from
    // UTF8 to UTF16 when writing to a console handle.

    if ((stream == stderr || stream == stdout) && size == 1 && !(count & ~uint32(0x7fffffff)) && !g_direct_lua_fwrite)
    {
        suppress_implicit_write_console_logging nolog;

        DWORD dw;
        HANDLE h = GetStdHandle(stream == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (GetConsoleMode(h, &dw))
        {
            // Maybe log.
            if (g_debug_log_terminal.get())
            {
                LOGCURSORPOS(h);
                LOG("%s \"%.*s\", %d", (stream == stderr) ? "LUACONERR" : "LUACONOUT", count, buffer, count);
#ifdef _MSC_VER
                if (g_debug_log_output_callstacks.get())
                {
                    char stk[8192];
                    format_callstack(2, 20, stk, sizeof(stk), false);
                    LOG("%s", stk);
                }
#endif
            }

            // g_printer is needed for terminal emulation.
            assertimplies(stream != stderr, g_printer);

            // Print the buffer.
            if (stream != stderr && g_printer)
            {
                g_printer->print(static_cast<const char*>(buffer), uint32(count));
            }
            else
            {
                // Convert to UTF16.
                wstr<32> s;
                str_iter tmpi(static_cast<const char*>(buffer), uint32(count));
                to_utf16(s, tmpi);
                // Write in a single OS console call.
                WriteConsoleW(h, s.c_str(), s.length(), &dw, nullptr);
            }
            return;
        }
    }

    fwrite(buffer, size, count, stream);
}



//------------------------------------------------------------------------------
#ifdef USE_MEMORY_TRACKING
extern "C" DECLALLOCATOR DECLRESTRICT void* __cdecl dbgluarealloc(void* pv, size_t size)
{
    pv = dbgrealloc_(pv, size, 0|memSkipOneFrame|memIgnoreLeak);
    if (pv)
    {
        dbgsetignore(pv);
        dbgsetlabel(pv, "LUA alloc", false);
    }
    return pv;
}
#endif
