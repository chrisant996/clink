// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state_lua.h"

#include <core/array.h>
#include <lib/line_state.h>

//------------------------------------------------------------------------------
static line_state_lua::method g_methods[] = {
    { "getline",      &line_state_lua::get_line },
    { "getwordcount", &line_state_lua::get_word_count },
    { "getwordinfo",  &line_state_lua::get_word_info },
    { "getword",      &line_state_lua::get_word },
    { "getendword",   &line_state_lua::get_end_word },
    {}
};



//------------------------------------------------------------------------------
line_state_lua::line_state_lua(const line_state& line)
: lua_bindable("line_state", g_methods)
, m_line(line)
{
}

//------------------------------------------------------------------------------
int line_state_lua::get_line(lua_State* state)
{
    lua_pushstring(state, m_line.get_line());
    return 1;
}

//------------------------------------------------------------------------------
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

    char delim[2] = { word.delim };
    lua_pushstring(state, "delim");
    lua_pushstring(state, delim);
    lua_rawset(state, -3);

    return 1;
}

//------------------------------------------------------------------------------
int line_state_lua::get_word_count(lua_State* state)
{
    lua_pushinteger(state, m_line.get_word_count());
    return 1;
}

//------------------------------------------------------------------------------
int line_state_lua::get_word(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;
    
    str<128> word;
    unsigned int index = int(lua_tointeger(state, 1)) - 1;
    if (!m_line.get_word(index, word))
        return 0;

    lua_pushstring(state, word.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int line_state_lua::get_end_word(lua_State* state)
{
    str<128> word;
    if (!m_line.get_end_word(word))
        return 0;

    lua_pushstring(state, word.c_str());
    return 1;
}
