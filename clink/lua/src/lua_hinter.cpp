// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_hinter.h"

#include <core/base.h>
#include <lib/line_state.h>
#include "lua_state.h"
#include "line_state_lua.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
void input_hint::clear()
{
    m_hint.free();
    m_pos = -1;
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
}

//------------------------------------------------------------------------------
bool input_hint::equals(const input_hint& other) const
{
    return (m_empty == other.m_empty &&
            m_pos == other.m_pos &&
            m_hint.equals(other.m_hint.c_str()));
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

    if (m_lua.pcall(state, 1, 2) != 0)
        goto nohint;

    if (!lua_isstring(state, -2))
        goto nohint;

    const char* hint = lua_tostring(state, -2);
    const int32 pos = lua_isnumber(state, -1) ? int32(lua_tointeger(state, -1)) : -1;
    out.set(hint, pos);
}
