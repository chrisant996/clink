// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

class matches;
struct lua_State;

//------------------------------------------------------------------------------
class matches_lua
    : public lua_bindable<matches_lua>
{
public:
                            matches_lua(matches& out);
                            ~matches_lua();

private:
    int                     add_match(lua_State* state);
    int                     add_matches(lua_State* state);
    int                     get_match(lua_State* state);
    int                     get_match_count(lua_State* state);
    int                     get_match_lcd(lua_State* state);
    int                     reset(lua_State* state);
    int                     set_file_handler(lua_State* state);
    matches&                m_matches;
    static method           s_methods[];
};
