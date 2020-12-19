// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <functional>

struct lua_State;

//------------------------------------------------------------------------------
class lua_state
{
public:
                    lua_state();
                    ~lua_state();
    void            initialise();
    void            shutdown();
    bool            do_string(const char* string, int length=-1);
    bool            do_file(const char* path);
    lua_State*      get_state() const;

    static int      pcall(lua_State* L, int nargs, int nresults);
    int             pcall(int nargs, int nresults) { return pcall(m_state, nargs, nresults); }

    bool            send_event(const char* event_name, std::function<bool(lua_State*)>* push_args=nullptr);

private:
    lua_State*      m_state;
};

//------------------------------------------------------------------------------
inline lua_State* lua_state::get_state() const
{
    return m_state;
}
