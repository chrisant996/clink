// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

#include <functional>
#include <list>

extern "C" {
#include <readline/readline.h>
#include <readline/rltypedefs.h>
}

struct lua_State;
class str_base;
class line_state;
typedef double lua_Number;

//------------------------------------------------------------------------------
enum class recognition : char { unrecognized = -1, unknown, executable, navigate, max };

//------------------------------------------------------------------------------
int checkinteger(lua_State* state, int index, bool* isnum=nullptr);
int optinteger(lua_State* state, int index, int default_value, bool* isnum=nullptr);
lua_Number checknumber(lua_State* state, int index, bool* isnum=nullptr);
lua_Number optnumber(lua_State* state, int index, lua_Number default_value, bool* isnum=nullptr);
const char* checkstring(lua_State* state, int index);
const char* optstring(lua_State* state, int index, const char* default_value);

//------------------------------------------------------------------------------
enum class lua_state_flags : int
{
    none            = 0x00,
    interpreter     = 0x01,
    no_env          = 0x02,
};
DEFINE_ENUM_FLAG_OPERATORS(lua_state_flags);

//------------------------------------------------------------------------------
class lua_state
{
public:
                    lua_state(lua_state_flags flags=lua_state_flags::none);
                    ~lua_state();
    void            initialise(lua_state_flags flags=lua_state_flags::none);
    void            shutdown();
    bool            do_string(const char* string, int length=-1);
    bool            do_file(const char* path);
    lua_State*      get_state() const;

    static bool     push_named_function(lua_State* L, const char* func_name, str_base* error=nullptr);

    static int      pcall(lua_State* L, int nargs, int nresults);
    static int      pcall_silent(lua_State* L, int nargs, int nresults);
    int             pcall(int nargs, int nresults) { return pcall(m_state, nargs, nresults); }
    int             pcall_silent(int nargs, int nresults) { return pcall_silent(m_state, nargs, nresults); }

    bool            send_event(const char* event_name, int nargs=0);
    bool            send_event_string_out(const char* event_name, str_base& out, int nargs=0);
    bool            send_event_cancelable(const char* event_name, int nargs=0);
    bool            send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out, std::list<str_moveable>* more_out=nullptr);
    bool            send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file);
    bool            call_lua_rl_global_function(const char* func_name, line_state* line);

#ifdef DEBUG
    void            dump_stack(int pos);
#endif

    static bool     is_in_luafunc() { return s_in_luafunc; }
    static bool     is_interpreter() { return s_interpreter; }

private:
    bool            send_event_internal(const char* event_name, const char* event_mechanism, int nargs=0, int nret=0);
    lua_State*      m_state;

    static bool     s_in_luafunc;
    static bool     s_interpreter;
};

//------------------------------------------------------------------------------
inline lua_State* lua_state::get_state() const
{
    return m_state;
}

//------------------------------------------------------------------------------
class save_stack_top
{
public:
    save_stack_top(lua_State* L);
    ~save_stack_top();
private:
    lua_State* const m_state;
    int const m_top;
};

//------------------------------------------------------------------------------
void get_lua_srcinfo(lua_State* L, str_base& out);

//------------------------------------------------------------------------------
// Dumps from pos to top of stack (use negative pos for relative position, use
// positive pos for absolute position, or use 0 for entire stack).
#ifdef DEBUG
void dump_lua_stack(lua_State* state, int pos=0);
#endif
