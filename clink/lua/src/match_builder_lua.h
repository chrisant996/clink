// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"
#include <lib/matches.h>
#include <memory>

class match_builder;
struct lua_State;
enum class match_type : unsigned char;

//------------------------------------------------------------------------------
class match_builder_lua
    : public lua_bindable<match_builder_lua>
{
public:
                    match_builder_lua(match_builder& builder);
                    match_builder_lua(std::shared_ptr<match_builder_toolkit>& toolkit);
                    ~match_builder_lua();
    int             add_match(lua_State* state);
    int             add_matches(lua_State* state);
    int             is_empty(lua_State* state);
    int             set_append_character(lua_State* state);
    int             set_suppress_append(lua_State* state);
    int             set_suppress_quoting(lua_State* state);
    int             set_no_sort(lua_State* state);
    int             set_volatile(lua_State* state);

    int             deprecated_add_match(lua_State* state);
    int             set_matches_are_files(lua_State* state);

    int             clear_toolkit(lua_State* state);

private:
    bool            add_match_impl(lua_State* state, int stack_index, match_type type);
    match_builder*  m_builder;
    std::shared_ptr<match_builder_toolkit> m_toolkit;

    friend class lua_bindable<match_builder_lua>;
    static const char* const c_name;
    static const method c_methods[];
};
