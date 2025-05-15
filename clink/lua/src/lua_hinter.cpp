// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_hinter.h"

#include <core/base.h>
#include <core/cwd_restorer.h>
#include <core/settings.h>
#include <lib/line_state.h>
#include <lib/display_readline.h>
#include "lua_state.h"
#include "line_state_lua.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
#define DEFAULT_INPUT_HINT_DELAY    500
#define MAXIMUM_INPUT_HINT_DELAY    3000

//------------------------------------------------------------------------------
static setting_int s_comment_row_hint_delay(
    "comment_row.hint_delay",
    "Delay before showing input hints",
    "Specifies a delay in milliseconds before showing input hints.\n"
    "The delay can be up to " AS_STR(MAXIMUM_INPUT_HINT_DELAY) " milliseconds, or 0 for no delay.\n"
    "The default is " AS_STR(DEFAULT_INPUT_HINT_DELAY) " milliseconds.",
    DEFAULT_INPUT_HINT_DELAY);

static setting_bool s_argmatcher_show_hints(
    "argmatcher.show_hints",
    "Show input hints from argmatchers",
    "When both the comment_row.show_hints and argmatcher.show_hints settings are\n"
    "enabled, argmatchers can show usage hints in the comment row (below the input\n"
    "line).",
    true);

//------------------------------------------------------------------------------
void input_hint::clear()
{
    m_hint.free();
    m_pos = -1;
    m_defer = 0;
    m_empty = true;
}

//------------------------------------------------------------------------------
void input_hint::set(const char* hint, int32 pos)
{
    if (!hint)
    {
        clear();
        return;
    }

    m_hint = hint;
    m_pos = pos;
    m_empty = false;

    m_defer = GetTickCount();
    if (!m_defer)
        ++m_defer;
}

//------------------------------------------------------------------------------
bool input_hint::equals(const input_hint& other) const
{
    return (m_empty == other.m_empty &&
            m_pos == other.m_pos &&
            m_hint.equals(other.m_hint.c_str()));
}

//------------------------------------------------------------------------------
DWORD input_hint::get_timeout() const
{
    if (!m_defer)
        return INFINITE;

    int32 input_hint_delay = s_comment_row_hint_delay.get();
    if (input_hint_delay < 0)
        input_hint_delay = DEFAULT_INPUT_HINT_DELAY;
    else if (input_hint_delay > MAXIMUM_INPUT_HINT_DELAY)
        input_hint_delay = MAXIMUM_INPUT_HINT_DELAY;

    const DWORD elapsed = GetTickCount() - m_defer;
    if (elapsed >= DWORD(input_hint_delay))
        return 0;

    return input_hint_delay - elapsed;
}

//------------------------------------------------------------------------------
void input_hint::clear_timeout()
{
    m_defer = 0;
}



//------------------------------------------------------------------------------
lua_hinter::lua_hinter(lua_state& lua)
: m_lua(lua)
{
}

//------------------------------------------------------------------------------
void lua_hinter::get_hint(const line_state& line, input_hint& out)
{
    if (!line.get_length())
    {
nohint:
        out.clear();
        return;
    }

    lua_State* state = m_lua.get_state();
    save_stack_top ss(state);

    // Call Lua to get hint
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_gethint");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    os::cwd_restorer cwd;

    if (m_lua.pcall(state, 1, 2) != 0)
        goto nohint;

    if (!lua_isstring(state, -2))
        goto nohint;

    const char* hint = lua_tostring(state, -2);
    const int32 pos = lua_isnumber(state, -1) ? int32(lua_tointeger(state, -1)) : -1;
    out.set(hint, pos);
}
