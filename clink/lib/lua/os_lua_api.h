// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class os_lua_api
{
public:
    static void         initialise(lua_State* state);

private:
    static int          chdir(lua_State* state);
    static int          getcwd(lua_State* state);
    static int          mkdir(lua_State* state);
    static int          rmdir(lua_State* state);
    static int          isdir(lua_State* state);
    static int          isfile(lua_State* state);
    static int          remove(lua_State* state);
    static int          rename(lua_State* state);
    static int          copy(lua_State* state);
    static int          glob_impl(lua_State* state, bool dirs_only);
    static int          globdirs(lua_State* state);
    static int          globfiles(lua_State* state);
    static int          getenv(lua_State* state);
    static const char*  get_string(lua_State* state, int index=0);
                        os_lua_api() = delete;
                        ~os_lua_api() = delete;
};
