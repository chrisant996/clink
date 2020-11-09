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
    void            initialise();
    void            shutdown();
    bool            do_string(const char* string, int length=-1);
    bool            do_file(const char* path);
    lua_State*      get_state() const;

    static int      pcall(lua_State* L, int nargs, int nresults);
    int             pcall(int nargs, int nresults) { return pcall(m_state, nargs, nresults); }

private:
    static int      error_handler(lua_State* L);

private:
    lua_State*      m_state;
    int             m_errfunc;
};

//------------------------------------------------------------------------------
inline lua_State* lua_state::get_state() const
{
    return m_state;
}
