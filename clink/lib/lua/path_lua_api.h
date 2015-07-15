// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct lua_State;

//------------------------------------------------------------------------------
class path_lua_api
{
public:
    void        initialise(lua_State* state);

private:
    int         clean(lua_State* state);
    int         get_base_name(lua_State* state);
    int         get_directory(lua_State* state);
    int         get_drive(lua_State* state);
    int         get_extension(lua_State* state);
    int         get_name(lua_State* state);
    int         join(lua_State* state);
    const char* get_string(lua_State* state, int index);
};
