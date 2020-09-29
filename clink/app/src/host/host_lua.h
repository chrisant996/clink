// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
class host_lua
{
public:
                        host_lua(const char* script_path = nullptr);
                        operator lua_state& ();
                        operator match_generator& ();
    void                load_scripts();

private:
    bool                load_scripts(const char* paths);
    void                load_script(const char* path);
    lua_state           m_state;
    lua_match_generator m_generator;
    str<>               m_script_path;
};
