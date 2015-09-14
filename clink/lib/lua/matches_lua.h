// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

class matches_builder;
struct lua_State;

//------------------------------------------------------------------------------
class matches_lua
    : public lua_bindable<matches_lua>
{
public:
                            matches_lua(matches_builder& result);
                            ~matches_lua();

private:
    int                     add_match(lua_State* state);
    int                     add_matches(lua_State* state);
    int                     get_match(lua_State* state);
    int                     get_match_count(lua_State* state);
    int                     clear_matches(lua_State* state);
    int                     get_match_lcd(lua_State* state);
    matches_builder&        m_builder;
    static method           s_methods[];
};
