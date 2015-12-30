// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches_lua.h"
#include "matches/matches.h"

#include <core/base.h>
#include <core/str.h>

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
matches_lua::matches_lua(matches_builder& builder)
: lua_bindable<matches_lua>("matches_lua", s_methods)
, m_builder(builder)
{
}

//------------------------------------------------------------------------------
matches_lua::~matches_lua()
{
}

//------------------------------------------------------------------------------
int matches_lua::add_match(lua_State* state)
{
    unsigned int current_count = m_builder.get_matches().get_match_count();

    if (const char* match = lua_tostring(state, 1))
    {
        if (lua_toboolean(state, 2))
            m_builder.add_match(match);
        else
            m_builder.consider_match(match);
    }

    unsigned int new_count = m_builder.get_matches().get_match_count();
    lua_pushboolean(state, (current_count < new_count));

    return 1;
}

//------------------------------------------------------------------------------
int matches_lua::add_matches(lua_State* state)
{
    if (!lua_istable(state, 1))
        return 0;

    int raw = lua_toboolean(state, 2);
    int match_count = (int)lua_rawlen(state, 1);
    for (int i = 0; i < match_count; ++i)
    {
        lua_rawgeti(state, -1, i + 1);

        if (const char* match = lua_tostring(state, -1))
        {
            if (raw)
                m_builder.add_match(match);
            else
                m_builder.consider_match(match);
        }

        lua_pop(state, 1);
    }

    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match(lua_State* state)
{
    int index = int(lua_tointeger(state, 1));
    if (const char* match = m_builder.get_matches().get_match(index - 1))
    {
        lua_pushstring(state, match);
        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match_count(lua_State* state)
{
    lua_pushinteger(state, m_builder.get_matches().get_match_count());
    return 1;
}

//------------------------------------------------------------------------------
int matches_lua::clear_matches(lua_State* state)
{
    m_builder.clear_matches();
    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match_lcd(lua_State* state)
{
    str<48> lcd;
    m_builder.get_matches().get_match_lcd(lcd);

    lua_pushstring(state, lcd.c_str());
    return 1;
}
