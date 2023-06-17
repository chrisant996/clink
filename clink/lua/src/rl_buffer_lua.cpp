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
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
}

#include <assert.h>

//------------------------------------------------------------------------------
const char* const rl_buffer_lua::c_name = "rl_buffer_lua";
const rl_buffer_lua::method rl_buffer_lua::c_methods[] = {
    { "getbuffer",      &get_buffer },
    { "getlength",      &get_length },
    { "getcursor",      &get_cursor },
    { "getanchor",      &get_anchor },
    { "setcursor",      &set_cursor },
    { "insert",         &insert },
    { "remove",         &remove },
    { "beginundogroup", &begin_undo_group },
    { "endundogroup",   &end_undo_group },
    { "beginoutput",    &begin_output },
    { "refreshline",    &refresh_line },
    { "getargument",    &get_argument },
    { "setargument",    &set_argument },
    { "ding",           &ding },
    {}
};



//------------------------------------------------------------------------------
rl_buffer_lua::rl_buffer_lua(line_buffer& buffer)
: m_rl_buffer(buffer)
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
/// -ver:   1.0.0
/// -ret:   string
/// Returns the current input line.
int32 rl_buffer_lua::get_buffer(lua_State* state)
{
    lua_pushlstring(state, m_rl_buffer.get_buffer(), m_rl_buffer.get_length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:getlength
/// -ver:   1.1.20
/// -ret:   integer
/// Returns the length of the input line.
int32 rl_buffer_lua::get_length(lua_State* state)
{
    lua_pushinteger(state, m_rl_buffer.get_length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:getcursor
/// -ver:   1.1.20
/// -ret:   integer
/// Returns the cursor position in the input line.  The value can be from 1 to
/// rl_buffer:getlength() + 1.  It can exceed the length of the input line
/// because the cursor can be positioned just past the end of the input line.
///
/// <strong>Note:</strong> In v1.1.20 through v1.2.31 this accidentally returned
/// 1 less than the actual cursor position.  In v1.2.32 and higher it returns
/// the correct cursor position.
int32 rl_buffer_lua::get_cursor(lua_State* state)
{
    lua_pushinteger(state, m_rl_buffer.get_cursor() + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:getanchor
/// -ver:   1.2.35
/// -ret:   integer | nil
/// Returns the anchor position of the currently selected text in the input
/// line, or nil if there is no selection.  The value can be from 1 to
/// rl_buffer:getlength() + 1.  It can exceed the length of the input line
/// because the anchor can be positioned just past the end of the input line.
int32 rl_buffer_lua::get_anchor(lua_State* state)
{
    int32 anchor = m_rl_buffer.get_anchor();
    if (anchor < 0)
        lua_pushnil(state);
    else
        lua_pushinteger(state, anchor + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:setcursor
/// -ver:   1.1.20
/// -arg:   cursor:integer
/// -ret:   integer
/// Sets the cursor position in the input line and returns the previous cursor
/// position.  <span class="arg">cursor</span> can be from 1 to
/// rl_buffer:getlength() + 1.  It can exceed the length of the input line
/// because the cursor can be positioned just past the end of the input line.
///
/// Note:  the input line is UTF8, and setting the cursor position inside a
/// multi-byte Unicode character may have undesirable results.
///
/// <strong>Note:</strong> In v1.1.20 through v1.2.31 this accidentally returned
/// 1 less than the actual cursor position.  In v1.2.32 and higher it returns
/// the correct cursor position.
int32 rl_buffer_lua::set_cursor(lua_State* state)
{
    bool isnum;
    uint32 old = m_rl_buffer.get_cursor() + 1;
    uint32 set = checkinteger(state, 1, &isnum) - 1;
    if (!isnum)
        return 0;

    m_rl_buffer.set_cursor(set);

    lua_pushinteger(state, old);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:insert
/// -ver:   1.1.20
/// -arg:   text:string
/// Inserts <span class="arg">text</span> at the cursor position in the input
/// line.
int32 rl_buffer_lua::insert(lua_State* state)
{
    const char* text = checkstring(state, 1);
    if (!text)
        return 0;

    m_rl_buffer.insert(text);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:remove
/// -ver:   1.1.20
/// -arg:   from:integer
/// -arg:   to:integer
/// Removes text from the input line starting at cursor position
/// <span class="arg">from</span> up to but not including
/// <span class="arg">to</span>.
///
/// If <span class="arg">from</span> is greater than <span class="arg">to</span>
/// then the positions are swapped before removing text.
///
/// Note:  the input line is UTF8, and removing only part of a multi-byte
/// Unicode character may have undesirable results.
int32 rl_buffer_lua::remove(lua_State* state)
{
    bool isnum1, isnum2;
    uint32 from = checkinteger(state, 1, &isnum1) - 1;
    uint32 to = checkinteger(state, 2, &isnum2) - 1;
    if (!isnum1 || !isnum2)
        return 0;

    m_rl_buffer.remove(from, to);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:beginundogroup
/// -ver:   1.1.20
/// Starts a new undo group.  This is useful for grouping together multiple
/// editing actions into a single undo operation.
int32 rl_buffer_lua::begin_undo_group(lua_State* state)
{
    m_num_undo++;
    m_rl_buffer.begin_undo_group();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:endundogroup
/// -ver:   1.1.20
/// Ends an undo group.  This is useful for grouping together multiple
/// editing actions into a single undo operation.
///
/// Note:  all undo groups are automatically ended when a key binding finishes
/// execution, so this function is only needed if a key binding needs to create
/// more than one undo group.
int32 rl_buffer_lua::end_undo_group(lua_State* state)
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
/// -ver:   1.1.20
/// Advances the output cursor to the next line after the Readline input buffer
/// so that subsequent output doesn't overwrite the input buffer display.
int32 rl_buffer_lua::begin_output(lua_State* state)
{
    if (!m_began_output)
    {
        end_prompt(true/*crlf*/);
        m_began_output = true;
    }
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:refreshline
/// -ver:   1.1.41
/// Redraws the input line.
int32 rl_buffer_lua::refresh_line(lua_State* state)
{
    rl_refresh_line(0, 0);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:getargument
/// -ver:   1.2.22
/// -ret:   integer | nil
/// Returns any accumulated numeric argument (<kbd>Alt</kbd>+Digits, etc), or
/// nil if no numeric argument has been entered.
int32 rl_buffer_lua::get_argument(lua_State* state)
{
    if (rl_explicit_arg)
    {
        lua_pushinteger(state, rl_numeric_arg);
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:setargument
/// -ver:   1.2.32
/// -arg:   argument:integer | nil
/// When <span class="arg">argument</span> is a number, it is set as the numeric
/// argument for use by Readline commands (as entered using
/// <kbd>Alt</kbd>+Digits, etc).  When <span class="arg">argument</span> is nil,
/// the numeric argument is cleared (having no numeric argument is different
/// from having 0 as the numeric argument).
int32 rl_buffer_lua::set_argument(lua_State* state)
{
    bool isnum;
    int32 arg = optinteger(state, 1, 0, &isnum);

    _rl_reset_argument();
    if (isnum)
    {
        rl_arg_sign = (arg < 0) ? -1 : 1;
        rl_explicit_arg = 1;
        rl_numeric_arg = arg;
    }
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl_buffer:ding
/// -ver:   1.1.20
/// Dings the bell.  If the
/// <code><a href="#configbellstyle">bell-style</a></code> Readline variable is
/// <code>visible</code> then it flashes the cursor instead.
int32 rl_buffer_lua::ding(lua_State* state)
{
    rl_ding();
    return 0;
}
