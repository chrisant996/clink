// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state_lua.h"

#include <core/array.h>
#include <lib/line_state.h>

//------------------------------------------------------------------------------
static line_state_lua::method g_methods[] = {
    { "getline",          &line_state_lua::get_line },
    { "getcursor",        &line_state_lua::get_cursor },
    { "getcommandoffset", &line_state_lua::get_command_offset },
    { "getwordcount",     &line_state_lua::get_word_count },
    { "getwordinfo",      &line_state_lua::get_word_info },
    { "getword",          &line_state_lua::get_word },
    { "getendword",       &line_state_lua::get_end_word },
    {}
};



//------------------------------------------------------------------------------
line_state_lua::line_state_lua(const line_state& line)
: lua_bindable("line_state", g_methods)
, m_line(line)
{
}

//------------------------------------------------------------------------------
/// -name:  line:getline
/// -ret:   string
/// Returns the current line in its entirety.
int line_state_lua::get_line(lua_State* state)
{
    lua_pushstring(state, m_line.get_line());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line:getcursor
/// -ret:   integer
/// Returns the position of the cursor.
int line_state_lua::get_cursor(lua_State* state)
{
    lua_pushinteger(state, m_line.get_cursor() + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line:getcommandoffset
/// -ret:   integer
/// -show:  -- Given the following line; abc& 123
/// -show:  -- where commands are separated by & symbols.
/// -show:  line:getcommandoffset() == 4
/// Returns the offset to the start of the delimited command in the line that's
/// being effectively edited. Note that this may not be the offset of the first
/// command of the line unquoted as whitespace isn't considered for words.
int line_state_lua::get_command_offset(lua_State* state)
{
    lua_pushinteger(state, m_line.get_command_offset() + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line:getwordcount
/// -ret:   integer
/// Returns the number of words in the current line.
int line_state_lua::get_word_count(lua_State* state)
{
    lua_pushinteger(state, m_line.get_word_count());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line:getwordinfo
/// -arg:   index:integer
/// -ret:   table
/// Returns a table of informationa about the Nth word in the line. The table
/// returned has the following scheme: <em>{ offset:integer, length:integer,
/// quoted:boolean, delim:boolean }</em>.
int line_state_lua::get_word_info(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;

    const array<word>& words = m_line.get_words();
    unsigned int index = int(lua_tointeger(state, 1)) - 1;
    if (index >= words.size())
        return 0;

    word word = *(words[index]);

    lua_createtable(state, 0, 4);

    lua_pushstring(state, "offset");
    lua_pushinteger(state, word.offset + 1);
    lua_rawset(state, -3);

    lua_pushstring(state, "length");
    lua_pushinteger(state, word.length);
    lua_rawset(state, -3);

    lua_pushstring(state, "quoted");
    lua_pushboolean(state, word.quoted);
    lua_rawset(state, -3);

    char delim[2] = { char(word.delim) };
    lua_pushstring(state, "delim");
    lua_pushstring(state, delim);
    lua_rawset(state, -3);

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line:getword
/// -arg:   index:integer
/// -ret:   string
/// Returns the word of the line at <em>index</em>.
int line_state_lua::get_word(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;

    unsigned int index = int(lua_tointeger(state, 1)) - 1;
    str_iter word = m_line.get_word(index);
    lua_pushlstring(state, word.get_pointer(), word.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line:getendword
/// -ret:   string
/// -show:  line:getword(line:getwordcount()) == line:getendword()
/// Returns the last word of the line. This is the word that matches are being
/// generated for.
int line_state_lua::get_end_word(lua_State* state)
{
    str_iter word = m_line.get_end_word();
    lua_pushlstring(state, word.get_pointer(), word.length());
    return 1;
}
