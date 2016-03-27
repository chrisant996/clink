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
    { "arefiles",       &matches_lua::set_file_handler },
    { "getmatch",       &matches_lua::get_match },
    { "getmatchcount",  &matches_lua::get_match_count },
    { "getmatchlcd",    &matches_lua::get_match_lcd },
    { "reset",          &matches_lua::reset },
    {}
};



//------------------------------------------------------------------------------
matches_lua::matches_lua(matches& out)
: lua_bindable<matches_lua>("matches_lua", s_methods)
, m_matches(out)
{
}

//------------------------------------------------------------------------------
matches_lua::~matches_lua()
{
}

//------------------------------------------------------------------------------
int matches_lua::add_match(lua_State* state)
{
    unsigned int current_count = m_matches.get_match_count();

    if (const char* match = lua_tostring(state, 1))
        m_matches.add_match(match);

    unsigned int new_count = m_matches.get_match_count();
    lua_pushboolean(state, (current_count < new_count));

    return 1;
}

//------------------------------------------------------------------------------
int matches_lua::add_matches(lua_State* state)
{
    if (!lua_istable(state, 1))
        return 0;

    int match_count = (int)lua_rawlen(state, 1);
    for (int i = 0; i < match_count; ++i)
    {
        lua_rawgeti(state, -1, i + 1);

        if (const char* match = lua_tostring(state, -1))
            m_matches.add_match(match);

        lua_pop(state, 1);
    }

    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match(lua_State* state)
{
    int index = int(lua_tointeger(state, 1));
    if (const char* match = m_matches.get_match(index - 1))
    {
        lua_pushstring(state, match);
        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::get_match_count(lua_State* state)
{
    lua_pushinteger(state, m_matches.get_match_count());
    return 1;
}

//------------------------------------------------------------------------------
int matches_lua::get_match_lcd(lua_State* state)
{
    str<48> lcd;
    m_matches.get_match_lcd(lcd);

    lua_pushstring(state, lcd.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int matches_lua::reset(lua_State* state)
{
    m_matches.reset();
    return 0;
}

//------------------------------------------------------------------------------
int matches_lua::set_file_handler(lua_State* state)
{
#if MODE4
    bool set = true;
    for (int i = 1, n = lua_gettop(state); i <= n; ++i)
        if (lua_isboolean(state, i))
            set &= (lua_toboolean(state, i) != 0);

    m_matches.set_handler(set ? get_file_match_handler() : nullptr);
#endif // MODE4
    return 0;
}
