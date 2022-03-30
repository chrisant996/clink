// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_state_lua.h"
#include "lua_word_classifications.h"

#include <vector>

class line_states;

//------------------------------------------------------------------------------
class line_states_lua
{
public:
                        line_states_lua(const line_states& lines);
                        line_states_lua(const line_states& lines, word_classifications& classifications);
                        ~line_states_lua() = default;
    void                push(lua_State* state);
    static void         make_new(lua_State* state, const line_states& lines);
private:
    std::vector<line_state_lua> m_lines;
    std::vector<lua_word_classifications> m_classifications;
};
