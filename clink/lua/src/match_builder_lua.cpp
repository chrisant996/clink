// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_builder_lua.h"

#include <core/base.h>
#include <core/str.h>
#include <lib/matches.h>

//------------------------------------------------------------------------------
static match_builder_lua::method g_methods[] = {
    { "add", &match_builder_lua::add },
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
int match_builder_lua::add(lua_State* state)
{
    int ret = 0;
    if (lua_gettop(state) > 0)
    {
        if (lua_istable(state, 1))
        {
            for (int i = 1, n = int(lua_rawlen(state, 1)); i <= n; ++i)
            {
                lua_rawgeti(state, 1, i);
                if (const char* match = lua_tostring(state, -1))
                    ret |= (m_builder.add_match(match) == true);

                lua_pop(state, 1);
            }
        }
        else if (const char* match = lua_tostring(state, 1))
            ret = m_builder.add_match(match), 1;
    }

    lua_pushboolean(state, ret);
    return 1;
}
