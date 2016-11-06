// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_builder_lua.h"

#include <core/base.h>
#include <core/str.h>
#include <lib/matches.h>

//------------------------------------------------------------------------------
static match_builder_lua::method g_methods[] = {
    { "addmatch",           &match_builder_lua::add_match },
    { "addmatches",         &match_builder_lua::add_matches },
    { "setprefixincluded",  &match_builder_lua::set_prefix_included },
    {}
};



//------------------------------------------------------------------------------
match_builder_lua::match_builder_lua(match_builder& builder)
: lua_bindable<match_builder_lua>("match_builder_lua", g_methods)
, m_builder(builder)
{
}

//------------------------------------------------------------------------------
match_builder_lua::~match_builder_lua()
{
}

//------------------------------------------------------------------------------
int match_builder_lua::add_match(lua_State* state)
{
    int ret = 0;
    if (lua_gettop(state) > 0)
        ret = !!add_match_impl(state, 1);

    lua_pushboolean(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
int match_builder_lua::set_prefix_included(lua_State* state)
{
    bool included = true;
    if (lua_gettop(state) > 0)
        included = (lua_toboolean(state, 1) != 0);

    m_builder.set_prefix_included(included);

    return 0;
}

//------------------------------------------------------------------------------
int match_builder_lua::add_matches(lua_State* state)
{
    if (lua_gettop(state) <= 0 || !lua_istable(state, 1))
    {
        lua_pushinteger(state, 0);
        lua_pushboolean(state, 0);
        return 2;
    }

    int count = 0;
    int total = int(lua_rawlen(state, 1));
    for (int i = 1; i <= total; ++i)
    {
        lua_rawgeti(state, 1, i);
        count += !!add_match_impl(state, -1);
        lua_pop(state, 1);
    }

    lua_pushinteger(state, count);
    lua_pushboolean(state, count == total);
    return 2;
}

//------------------------------------------------------------------------------
bool match_builder_lua::add_match_impl(lua_State* state, int stack_index)
{
    if (lua_isstring(state, stack_index))
    {
        const char* match = lua_tostring(state, stack_index);
        return m_builder.add_match(match);
    }
    else if (lua_istable(state, stack_index))
    {
        if (stack_index < 0)
            --stack_index;

        match_desc desc = {};

        lua_pushliteral(state, "match");
        lua_rawget(state, stack_index);
        if (lua_isstring(state, -1))
            desc.match = lua_tostring(state, -1);
        lua_pop(state, 1);

        lua_pushliteral(state, "displayable");
        lua_rawget(state, stack_index);
        if (lua_isstring(state, -1))
            desc.displayable = lua_tostring(state, -1);
        lua_pop(state, 1);

        lua_pushliteral(state, "aux");
        lua_rawget(state, stack_index);
        if (lua_isstring(state, -1))
            desc.aux = lua_tostring(state, -1);
        lua_pop(state, 1);

        lua_pushliteral(state, "suffix");
        lua_rawget(state, stack_index);
        if (lua_isstring(state, -1))
            desc.suffix = lua_tostring(state, -1)[0];
        lua_pop(state, 1);

        if (desc.match != nullptr)
            return m_builder.add_match(desc);
    }

    return false;
}
