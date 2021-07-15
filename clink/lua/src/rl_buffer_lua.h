// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "core/str.h"

#include "lua_bindable.h"

struct lua_State;
class line_buffer;

//------------------------------------------------------------------------------
class rl_buffer_lua
    : public lua_bindable<rl_buffer_lua>
{
public:
                            rl_buffer_lua(line_buffer& buffer);
                            ~rl_buffer_lua();

    int                     get_buffer(lua_State* state);
    int                     get_length(lua_State* state);
    int                     get_cursor(lua_State* state);
    int                     set_cursor(lua_State* state);
    int                     insert(lua_State* state);
    int                     remove(lua_State* state);
    int                     begin_undo_group(lua_State* state);
    int                     end_undo_group(lua_State* state);
    int                     begin_output(lua_State* state);
    int                     refresh_line(lua_State* state);
    int                     get_argument(lua_State* state);
    int                     ding(lua_State* state);

private:
    line_buffer&            m_rl_buffer;
    int                     m_num_undo = 0;
    bool                    m_began_output = false;
};
