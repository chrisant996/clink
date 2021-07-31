// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <functional>

extern "C" {
#include <readline/readline.h>
#include <readline/rltypedefs.h>
}

struct lua_State;
class str_base;

//------------------------------------------------------------------------------
int checkinteger(lua_State* state, int index, bool* isnum=nullptr);
int optinteger(lua_State* state, int index, int default_value, bool* isnum=nullptr);
const char* checkstring(lua_State* state, int index);
const char* optstring(lua_State* state, int index, const char* default_value);

//------------------------------------------------------------------------------
class lua_state
{
public:
                    lua_state();
                    ~lua_state();
    void            initialise();
    void            shutdown();
    bool            do_string(const char* string, int length=-1);
    bool            do_file(const char* path);
    lua_State*      get_state() const;

    static bool     push_named_function(lua_State* L, const char* func_name, str_base* error=nullptr);

    static int      pcall(lua_State* L, int nargs, int nresults);
    int             pcall(int nargs, int nresults) { return pcall(m_state, nargs, nresults); }

    bool            send_event(const char* event_name, int nargs=0);
    bool            send_event_cancelable(const char* event_name, int nargs=0);
    bool            send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out);
    bool            call_lua_rl_global_function(const char* func_name);

    void            print_error(const char* error);

#ifdef DEBUG
    void            dump_stack(int pos);
#endif

    static bool     is_in_luafunc() { return s_in_luafunc; }

private:
    bool            send_event_internal(const char* event_name, const char* event_mechanism, int nargs=0, int nret=0);
    lua_State*      m_state;

    static bool     s_in_luafunc;
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
// Dumps from pos to top of stack (use negative pos for relative position, use
// positive pos for absolute position, or use 0 for entire stack).
#ifdef DEBUG
void dump_lua_stack(lua_State* state, int pos=0);
#endif
