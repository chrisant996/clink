// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"
#include <lib/matches.h>
#include <memory>

class match_builder;
struct lua_State;
enum class match_type : unsigned short;

//------------------------------------------------------------------------------
class match_builder_lua
    : public lua_bindable<match_builder_lua>
{
public:
                    match_builder_lua(match_builder& builder);
                    match_builder_lua(std::shared_ptr<match_builder_toolkit>& toolkit);
                    ~match_builder_lua();

    int32           do_add_matches(lua_State* state, bool self_on_stack);

protected:
    int32           add_match(lua_State* state);
    int32           add_matches(lua_State* state);
    int32           is_empty(lua_State* state);
    int32           set_append_character(lua_State* state);
    int32           set_suppress_append(lua_State* state);
    int32           set_suppress_quoting(lua_State* state);
    int32           set_force_quoting(lua_State* state);
    int32           set_fully_qualify(lua_State* state);
    int32           set_no_sort(lua_State* state);
    int32           set_volatile(lua_State* state);

    int32           deprecated_add_match(lua_State* state);
    int32           set_matches_are_files(lua_State* state);

    int32           clear_toolkit(lua_State* state);
    int32           set_input_line(lua_State* state);
    int32           matches_ready(lua_State* state);
    int32           get_generation_id(lua_State* state);
    int32           log_matches(lua_State* state);

private:
    bool            add_match_impl(lua_State* state, int32 stack_index, match_type type);
    match_builder*  m_builder;
    std::shared_ptr<match_builder_toolkit> m_toolkit;

    friend class lua_bindable<match_builder_lua>;
    static const char* const c_name;
    static const method c_methods[];
};
