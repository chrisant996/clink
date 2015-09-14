// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_result_lua.h"
#include "core/base.h"
#include "core/str.h"

//------------------------------------------------------------------------------
matches_lua::method matches_lua::s_methods[] = {
    { "addmatch",       &matches_lua::add_match },
    { "addmatches",     &matches_lua::add_matches },
    { "getmatch",       &matches_lua::get_match },
    { "getmatchcount",  &matches_lua::get_match_count },
    { "clearmatches",   &matches_lua::clear_matches },
    { "getmatchlcd",    &matches_lua::get_match_lcd },
    {}
};



//------------------------------------------------------------------------------
matches_lua::matches_lua(matches& result)
: lua_bindable<matches_lua>("matches_lua", s_methods)
, m_result(result)
, m_builder(result, "")
{
}

//------------------------------------------------------------------------------
matches_lua::~matches_lua()
{
}

//------------------------------------------------------------------------------
int matches_lua::add_match(lua_State* state)
{
    unsigned int current_count = m_result.get_match_count();

    if (const char* match = lua_tostring(state, 1))
    {
        if (lua_toboolean(state, 2))
            m_result.add_match(match);
        else
            m_builder << match;
    }

    lua_pushboolean(state, (current_count < m_result.get_match_count()));
    return 1;
}

//------------------------------------------------------------------------------
int matches_lua::add_matches(lua_State* state)
{
    if (!lua_istable(state, -1))
        return 0;

    int match_count = (int)lua_rawlen(state, -1);
    for (int i = 0; i < match_count; ++i)
    {
        lua_rawgeti(state, -1, i + 1);

        if (const char* match = lua_tostring(state, -1))
            m_result.add_match(match);

        lua_pop(state, 1);
    }

    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match(lua_State* state)
{
    int index = int(lua_tointeger(state, 1));
    if (const char* match = m_result.get_match(index - 1))
    {
        lua_pushstring(state, match);
        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match_count(lua_State* state)
{
    lua_pushinteger(state, m_result.get_match_count());
    return 1;
}

//------------------------------------------------------------------------------
int matches_lua::clear_matches(lua_State* state)
{
    m_result.clear_matches();
    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match_lcd(lua_State* state)
{
    str<48> lcd;
    m_result.get_match_lcd(lcd);

    lua_pushstring(state, lcd.c_str());
    return 1;
}
