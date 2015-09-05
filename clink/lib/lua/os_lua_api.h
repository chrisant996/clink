// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class os_lua_api
{
public:
    void        initialise(lua_State* state);

private:
    int         chdir(lua_State* state);
    int         getcwd(lua_State* state);
    int         mkdir(lua_State* state);
    int         rmdir(lua_State* state);
    int         isdir(lua_State* state);
    int         isfile(lua_State* state);
    int         remove(lua_State* state);
    int         rename(lua_State* state);
    int         copy(lua_State* state);
    int         getenv(lua_State* state);
    const char* get_string(lua_State* state, int index=0);
};
