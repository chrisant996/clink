// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct lua_State;

//------------------------------------------------------------------------------
class lua_state
{
public:
                     lua_state();
                     ~lua_state();
    void             initialise(bool use_debugger=false);
    void             shutdown();
    bool             do_string(const char* string);
    bool             do_file(const char* path);
    lua_State*       get_state() const;

private:
    lua_State*       m_state;
};

//------------------------------------------------------------------------------
inline lua_State* lua_state::get_state() const
{
    return m_state;
}
