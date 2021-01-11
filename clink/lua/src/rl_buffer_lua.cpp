// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_buffer_lua.h"
#include "lua_state.h"
#include "lib/line_buffer.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <readline/readline.h>
extern void _rl_move_vert(int);
extern int _rl_vis_botlin;
}

#include <assert.h>

//------------------------------------------------------------------------------
static rl_buffer_lua::method g_methods[] = {
    { "getbuffer",      &rl_buffer_lua::get_buffer },
    { "getlength",      &rl_buffer_lua::get_length },
    { "getcursor",      &rl_buffer_lua::get_cursor },
    { "setcursor",      &rl_buffer_lua::set_cursor },
    { "insert",         &rl_buffer_lua::insert },
    { "remove",         &rl_buffer_lua::remove },
    { "beginundogroup", &rl_buffer_lua::begin_undo_group },
    { "endundogroup",   &rl_buffer_lua::end_undo_group },
    { "beginoutput",    &rl_buffer_lua::begin_output },
    { "ding",           &rl_buffer_lua::ding },
    {}
};



//------------------------------------------------------------------------------
rl_buffer_lua::rl_buffer_lua(line_buffer& buffer)
: lua_bindable("rl_buffer", g_methods)
, m_rl_buffer(buffer)
{
}

//------------------------------------------------------------------------------
rl_buffer_lua::~rl_buffer_lua()
{
    while (m_num_undo > 0)
    {
        m_rl_buffer.end_undo_group();
        m_num_undo--;
    }

    if (m_began_output)
        m_rl_buffer.redraw();
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:getbuffer
/// -ret:   string
/// Returns the current input line.
int rl_buffer_lua::get_buffer(lua_State* state)
{
    lua_pushlstring(state, m_rl_buffer.get_buffer(), m_rl_buffer.get_length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:getlength
/// -ret:   integer
/// Returns the length of the input line.
int rl_buffer_lua::get_length(lua_State* state)
{
    lua_pushinteger(state, m_rl_buffer.get_length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:getcursor
/// -ret:   integer
/// Returns the cursor position in the input line.
int rl_buffer_lua::get_cursor(lua_State* state)
{
    lua_pushinteger(state, m_rl_buffer.get_cursor());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:setcursor
/// -arg:   cursor:integer
/// -ret:   integer
/// Sets the cursor position in the input line and returns the previous cursor
/// position.  <span class="arg">cursor</span> can be from 1 to
/// rl_buffer:getlength().
///
/// Note:  the input line is UTF8, and setting the cursor position inside a
/// multi-byte Unicode character may have undesirable results.
int rl_buffer_lua::set_cursor(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;

    unsigned int old = m_rl_buffer.get_cursor();
    unsigned int set = int(lua_tointeger(state, 1)) - 1;
    m_rl_buffer.set_cursor(set);

    lua_pushinteger(state, old);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:insert
/// -arg:   text:string
/// Inserts <span class="arg">text</span> at the cursor position in the input
/// line.
int rl_buffer_lua::insert(lua_State* state)
{
    if (!lua_isstring(state, 1))
        return 0;

    m_rl_buffer.insert(lua_tostring(state, 1));
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:remove
/// -arg:   from:integer
/// -arg:   to:integer
/// Removes text from the input line starting at cursor position
/// <span class="arg">from</span> through <span class="arg">to</span>.
///
/// Note:  the input line is UTF8, and removing only part of a multi-byte
/// Unicode character may have undesirable results.
int rl_buffer_lua::remove(lua_State* state)
{
    if (!lua_isnumber(state, 1) ||
        !lua_isnumber(state, 2))
        return 0;

    unsigned int from = int(lua_tointeger(state, 1)) - 1;
    unsigned int to = int(lua_tointeger(state, 2)) - 1;

    m_rl_buffer.remove(from, to);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:beginundogroup
/// Starts a new undo group.  This is useful for grouping together multiple
/// editing actions into a single undo operation.
int rl_buffer_lua::begin_undo_group(lua_State* state)
{
    m_num_undo++;
    m_rl_buffer.begin_undo_group();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:endundogroup
/// Ends an undo group.  This is useful for grouping together multiple
/// editing actions into a single undo operation.
///
/// Note:  all undo groups are automatically ended when a key binding finishes
/// execution, so this function is only needed if a key binding needs to create
/// more than one undo group.
int rl_buffer_lua::end_undo_group(lua_State* state)
{
    if (m_num_undo > 0)
    {
        m_rl_buffer.end_undo_group();
        m_num_undo--;
    }
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:beginoutput
/// Advances the output cursor to the next line after the Readline input buffer
/// so that subsequent output doesn't overwrite the input buffer display.
int rl_buffer_lua::begin_output(lua_State* state)
{
    if (!m_began_output)
    {
        _rl_move_vert(_rl_vis_botlin);
        puts("");
        m_began_output = true;
    }
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:ding
/// Dings the bell.  If the <code>bell-style</code> Readline variable is
/// <code>visible</code> then it flashes the cursor instead.
int rl_buffer_lua::ding(lua_State* state)
{
    rl_ding();
    return 0;
}
