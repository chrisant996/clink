// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "matches_lua.h"

#include <lib/matches.h>

//------------------------------------------------------------------------------
const char* const matches_lua::c_name = "matches_lua";
const matches_lua::method matches_lua::c_methods[] = {
    { "getprefix",              &get_prefix },
    { "getcount",               &get_count },
    { "getmatch",               &get_match },
    { "gettype",                &get_type },
    {}
};



//------------------------------------------------------------------------------
matches_lua::matches_lua(const matches& matches)
: m_matches(&matches)
{
}

//------------------------------------------------------------------------------
matches_lua::matches_lua(std::shared_ptr<match_builder_toolkit>& toolkit)
: m_matches(toolkit.get()->get_matches())
, m_toolkit(toolkit)
{
}

//------------------------------------------------------------------------------
matches_lua::~matches_lua()
{
}

//------------------------------------------------------------------------------
/// -name:  matches:getprefix
/// -ver:   1.2.47
/// -ret:   string
/// Returns the longest common prefix of the available matches.
int32 matches_lua::get_prefix(lua_State* state)
{
    if (!m_has_prefix)
    {
        m_matches->get_lcd(m_prefix);
        m_has_prefix = true;
    }

    lua_pushlstring(state, m_prefix.c_str(), m_prefix.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  matches:getcount
/// -ver:   1.2.47
/// -ret:   integer
/// Returns the number of available matches.
int32 matches_lua::get_count(lua_State* state)
{
    lua_pushinteger(state, m_matches->get_match_count());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  matches:getmatch
/// -ver:   1.2.47
/// -arg:   index:integer
/// -ret:   string
/// Returns the match text for the <span class="arg">index</span> match.
int32 matches_lua::get_match(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    if (!_index.isnum())
        return 0;
    const uint32 index = _index - 1;

    if (index >= m_matches->get_match_count())
        return 0;

    lua_pushstring(state, m_matches->get_match(index));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  matches:gettype
/// -ver:   1.2.47
/// -arg:   index:integer
/// -ret:   string
/// Returns the match type for the <span class="arg">index</span> match.
int32 matches_lua::get_type(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    if (!_index.isnum())
        return 0;
    const uint32 index = _index - 1;

    if (index >= m_matches->get_match_count())
        return 0;

    str<> type;
    match_type_to_string(m_matches->get_match_type(index), type);
    lua_pushlstring(state, type.c_str(), type.length());
    return 1;
}
