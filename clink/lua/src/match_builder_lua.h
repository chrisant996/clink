// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

class match_builder;
struct lua_State;
enum class match_type : unsigned char;

//------------------------------------------------------------------------------
class match_builder_lua
    : public lua_bindable<match_builder_lua>
{
public:
                    match_builder_lua(match_builder& builder);
                    ~match_builder_lua();
    int             add_match(lua_State* state);
    int             add_matches(lua_State* state);
    int             set_append_character(lua_State* state);
    int             set_suppress_append(lua_State* state);
    int             set_suppress_quoting(lua_State* state);

    int             deprecated_add_match(lua_State* state);
    int             set_matches_are_files(lua_State* state);

private:
    bool            add_match_impl(lua_State* state, int stack_index, match_type type);
    match_builder&  m_builder;
};
