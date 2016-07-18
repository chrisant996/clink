// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

class line_state;
struct lua_State;

//------------------------------------------------------------------------------
class line_state_lua
    : public lua_bindable<line_state_lua>
{
public:
                        line_state_lua(const line_state& line);
    int                 get_line(lua_State* state);
    int                 get_cursor(lua_State* state);
    int                 get_command_offset(lua_State* state);
    int                 get_word_count(lua_State* state);
    int                 get_word_info(lua_State* state);
    int                 get_word(lua_State* state);
    int                 get_end_word(lua_State* state);

private:
    const line_state&   m_line;
};
