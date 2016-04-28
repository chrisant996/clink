// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

class match_builder;
struct lua_State;

//------------------------------------------------------------------------------
class match_builder_lua
    : public lua_bindable<match_builder_lua>
{
public:
                    match_builder_lua(match_builder& builder);
                    ~match_builder_lua();
    int             add(lua_State* state);

private:
    match_builder&  m_builder;
};
