// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "clink_lua_api.h"
#include "lua_match_generator.h"

struct lua_State;

//------------------------------------------------------------------------------
class lua_root
    : public lua_match_generator
{
public:
                     lua_root();
                     ~lua_root();
    void             initialise();
    void             shutdown();
    bool             do_string(const char* string);
    bool             do_file(const char* path);
    lua_State*       get_state() const;

private:
    lua_State*       m_state;
    clink_lua_api    m_clink;
};

//------------------------------------------------------------------------------
inline lua_State* lua_root::get_state() const
{
    return m_state;
}
