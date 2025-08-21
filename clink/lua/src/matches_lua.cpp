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
    { "getdescription",         &get_description },
    { "getappendchar",          &get_append_char },
    { "getsuppressquoting",     &get_suppress_quoting },
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

//------------------------------------------------------------------------------
/// -name:  matches:getdescription
/// -ver:   1.7.23
/// -arg:   index:integer
/// -ret:   string
/// Returns the match description for the <span class="arg">index</span> match.
int32 matches_lua::get_description(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    if (!_index.isnum())
        return 0;
    const uint32 index = _index - 1;

    if (index >= m_matches->get_match_count())
        return 0;

    const char* desc = m_matches->get_match_description(index);
    lua_pushstring(state, desc ? desc : "");
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  matches:getappendchar
/// -ver:   1.7.23
/// -arg:   index:integer
/// -ret:   string
/// Returns what the completion generator suggested should be appended after
/// the match.
int32 matches_lua::get_append_char(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    if (!_index.isnum())
        return 0;
    const uint32 index = _index - 1;

    if (index >= m_matches->get_match_count())
        return 0;

    shadow_bool suppress = m_matches->get_match_suppress_append(index);
    if (!suppress.is_explicit())
        suppress.set_explicit(m_matches->is_suppress_append());

    str<16> tmp;
    if (!suppress.get())
    {
        char c = m_matches->get_match_append_char(index);
        if (!c && !is_match_type(m_matches->get_match_type(index), match_type::dir))
            c = ' ';
        if (c)
            tmp.format("%c", c);
    }
    lua_pushlstring(state, tmp.c_str(), tmp.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  matches:getsuppressquoting
/// -ver:   1.7.23
/// -ret:   boolean
/// Returns whether the completion generator indicated that automatic quoting
/// should be suppressed for the matches.
int32 matches_lua::get_suppress_quoting(lua_State* state)
{
    lua_pushboolean(state, !!m_matches->get_suppress_quoting());
    return 1;
}
