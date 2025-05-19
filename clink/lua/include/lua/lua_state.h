// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>
#include <lib/recognizer.h>

#include <functional>
#include <list>

extern "C" {
#include <readline/readline.h>
#include <readline/rltypedefs.h>
}

struct lua_State;
class str_base;
class line_state;
class terminal_in;
class terminal_out;
typedef double lua_Number;

#define LUA_SELF    (1)

//------------------------------------------------------------------------------
template<class T>
class checked_num
{
public:
                    checked_num(T value, bool isnum=true) : m_value(value), m_isnum(isnum) {}

    bool            isnum() const { return m_isnum; }
    const T         get() const { return m_value; }
                    operator T() const { return m_value; }

    void            minus_one() { --m_value; }  // For converting to zero-based.
    void            plus_one() { ++m_value; }   // For converting to one-based.

private:
    T               m_value;
    const bool      m_isnum;
};

//------------------------------------------------------------------------------
checked_num<int32> checkinteger(lua_State* L, int32 index);
checked_num<int32> optinteger(lua_State* L, int32 index, int32 default_value);
checked_num<lua_Number> checknumber(lua_State* L, int32 index);
checked_num<lua_Number> optnumber(lua_State* L, int32 index, lua_Number default_value);
const char* checkstring(lua_State* L, int32 index);
const char* optstring(lua_State* L, int32 index, const char* default_value);

//------------------------------------------------------------------------------
enum class lua_state_flags : int32
{
    none            = 0x00,
    interpreter     = 0x01,
    no_env          = 0x02,
};
DEFINE_ENUM_FLAG_OPERATORS(lua_state_flags);

//------------------------------------------------------------------------------
class lua_state
{
    friend void lua_load_script_impl(lua_state& state, const char* path, int32 length);

public:
                    lua_state(lua_state_flags flags=lua_state_flags::none);
                    ~lua_state();
    void            initialise(lua_state_flags flags=lua_state_flags::none);
    void            shutdown();
    bool            do_string(const char* string, int32 length=-1, str_base* error=nullptr, const char* name=nullptr);
    bool            do_file(const char* path);
    lua_State*      get_state() const;

    static bool     push_named_function(lua_State* L, const char* func_name, str_base* error=nullptr);

    static int32    pcall(lua_State* L, int32 nargs, int32 nresults, str_base* error=nullptr);
    static int32    pcall_silent(lua_State* L, int32 nargs, int32 nresults);
    int32           pcall(int32 nargs, int32 nresults, str_base* error=nullptr) { return pcall(m_state, nargs, nresults, error); }
    int32           pcall_silent(int32 nargs, int32 nresults) { return pcall_silent(m_state, nargs, nresults); }

    static void     activate_clinkprompt_module(lua_State* L, const char* module);
    static void     load_colortheme_in_memory(lua_State* L, const char* theme);
    static bool     send_event(lua_State* L, const char* event_name, int32 nargs=0);
    void            activate_clinkprompt_module(const char* module);
    void            load_colortheme_in_memory(const char* theme);
    bool            send_event(const char* event_name, int32 nargs=0);
    bool            send_event_string_out(const char* event_name, str_base& out, int32 nargs=0);
    bool            send_event_cancelable(const char* event_name, int32 nargs=0);
    bool            send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out, std::list<str_moveable>* more_out=nullptr);
    bool            send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file);
    bool            send_oninputlinechanged_event(const char* line);
    bool            get_command_word(line_state& line, str_base& command_word, bool& quoted, recognition& recog, str_base& file);
    bool            call_lua_rl_global_function(const char* func_name, const line_state* line);
    static int32    call_onfiltermatches(lua_State* L, int32 nargs, int32 nresults);

#ifdef DEBUG
    void            dump_stack(int32 pos=0);
#endif

    static bool     is_in_luafunc() { return s_in_luafunc; }
    static bool     is_in_onfiltermatches() { return s_in_onfiltermatches; }
    static bool     is_interpreter() { return s_interpreter; }
    static bool     is_internal() { return s_internal; }

    static void     set_internal(bool internal) { s_internal = internal; }

    static uint32   save_global_states(bool new_coroutine);
    static void     restore_global_states(uint32 states);

private:
    static bool     send_event_internal(lua_State* L, const char* event_name, const char* event_mechanism, int32 nargs=0, int32 nret=0);
    lua_State*      m_state;

    static bool     s_internal;
    static bool     s_interpreter;
    static bool     s_in_luafunc;
    static bool     s_in_onfiltermatches;
#ifdef DEBUG
    static bool     s_in_coroutine;
#endif
};

//------------------------------------------------------------------------------
#ifndef DEBUG
inline lua_State* lua_state::get_state() const
{
    return m_state;
}
#endif

//------------------------------------------------------------------------------
class save_stack_top
{
public:
    save_stack_top(lua_State* L);
    ~save_stack_top();
private:
    lua_State* const m_state;
    int32 const m_top;
};

//------------------------------------------------------------------------------
void get_lua_srcinfo(lua_State* L, str_base& out);
void set_lua_terminal(terminal_in* in, terminal_out* out);
terminal_in* get_lua_terminal_input();
terminal_out* get_lua_terminal_output();
void set_lua_conout(FILE* file);
bool is_lua_conout(FILE* file);

//------------------------------------------------------------------------------
// Dumps from pos to top of stack (use negative pos for relative position, use
// positive pos for absolute position, or use 0 for entire stack).
#ifdef DEBUG
void dump_lua_stack(lua_State* L, int32 pos=0);
#endif

//------------------------------------------------------------------------------
#define LUA_ONLYONMAIN(L, name) \
    do { \
        const bool ismain = (G(L)->mainthread == L); \
        if (!ismain) \
            return luaL_error(L, LUA_QS " may only be used in the main coroutine", name); \
    } while (false)

