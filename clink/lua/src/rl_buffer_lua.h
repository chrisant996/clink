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

    void                    do_begin_output();

protected:
    int32                   get_buffer(lua_State* state);
    int32                   get_length(lua_State* state);
    int32                   get_cursor(lua_State* state);
    int32                   get_anchor(lua_State* state);
    int32                   set_cursor(lua_State* state);
    int32                   insert(lua_State* state);
    int32                   remove(lua_State* state);
    int32                   begin_undo_group(lua_State* state);
    int32                   end_undo_group(lua_State* state);
    int32                   begin_output(lua_State* state);
    int32                   refresh_line(lua_State* state);
    int32                   get_argument(lua_State* state);
    int32                   set_argument(lua_State* state);
    int32                   has_suggestion(lua_State* state);
    int32                   insert_suggestion(lua_State* state);
    int32                   set_comment_row(lua_State* state);
    int32                   ding(lua_State* state);

private:
    line_buffer&            m_rl_buffer;
    int32                   m_num_undo = 0;
    bool                    m_began_output = false;

    friend class lua_bindable<rl_buffer_lua>;
    static const char* const c_name;
    static const method     c_methods[];
};
