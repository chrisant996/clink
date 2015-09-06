// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct lua_State;

//------------------------------------------------------------------------------
class clink_lua_api
{
public:
    static void initialise(lua_State* state);

private:
    static int  to_lowercase(lua_State* state);
    static int  matches_are_files(lua_State* state);
    static int  get_env(lua_State* state);
    static int  get_env_var_names(lua_State* state);
    static int  get_setting_str(lua_State* state);
    static int  get_setting_int(lua_State* state);
    static int  get_rl_variable(lua_State* state);
    static int  is_rl_variable_true(lua_State* state);
    static int  get_host_process(lua_State* state);
    static int  get_console_aliases(lua_State* state);
    static int  get_screen_info(lua_State* state);
                clink_lua_api() = delete;
                ~clink_lua_api() = delete;
};
