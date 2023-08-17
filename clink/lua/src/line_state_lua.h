// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

class line_state;
class line_state_copy;
struct lua_State;

//------------------------------------------------------------------------------
line_state_copy* make_line_state_copy(const line_state& line);

//------------------------------------------------------------------------------
class line_state_lua
    : public lua_bindable<line_state_lua>
{
public:
                        line_state_lua(const line_state& line);
                        line_state_lua(line_state_copy* copy);
                        ~line_state_lua();
    int32               get_line(lua_State* state);
    int32               get_cursor(lua_State* state);
    int32               get_command_offset(lua_State* state);
    int32               get_command_word_index(lua_State* state);
    int32               get_word_count(lua_State* state);
    int32               get_word_info(lua_State* state);
    int32               get_word(lua_State* state);
    int32               get_end_word(lua_State* state);
    int32               shift(lua_State* state);

private:
    const line_state*   m_line;
    line_state_copy*    m_copy;
    uint32              m_shift = 0;

    friend class lua_bindable<line_state_lua>;
    static const char* const c_name;
    static const line_state_lua::method c_methods[];
};
