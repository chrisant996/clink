// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class lua_state;
struct lua_State;

//------------------------------------------------------------------------------
class lua_state
{
public:
                    lua_state(bool enable_debugger=false);
                    ~lua_state();
    void            initialise();
    void            shutdown();
    bool            do_string(const char* string);
    bool            do_file(const char* path);
    lua_State*      get_state() const;

private:
    lua_State*      m_state;
    bool            m_enable_debugger;
};

//------------------------------------------------------------------------------
inline lua_State* lua_state::get_state() const
{
    return m_state;
}
