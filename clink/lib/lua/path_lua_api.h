// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct lua_State;

//------------------------------------------------------------------------------
class path_lua_api
{
public:
    static void         initialise(lua_State* state);

private:
    static int          clean(lua_State* state);
    static int          get_base_name(lua_State* state);
    static int          get_directory(lua_State* state);
    static int          get_drive(lua_State* state);
    static int          get_extension(lua_State* state);
    static int          get_name(lua_State* state);
    static int          join(lua_State* state);
    static const char*  get_string(lua_State* state, int index);
                        path_lua_api() = delete;
                        ~path_lua_api() = delete;
};
