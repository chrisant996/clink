// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct lua_State;

//------------------------------------------------------------------------------
class clink_lua_api
{
public:
                    clink_lua_api();
    void            initialise(lua_State* state);

protected:
    int             change_dir(lua_State* state);
    int             to_lowercase(lua_State* state);
    int             find_files_impl(lua_State* state, bool dirs_only);
    int             find_files(lua_State* state);
    int             find_dirs(lua_State* state);
    int             matches_are_files(lua_State* state);
    int             get_env(lua_State* state);
    int             get_env_var_names(lua_State* state);
    int             get_setting_str(lua_State* state);
    int             get_setting_int(lua_State* state);
    int             is_dir(lua_State* state);
    int             get_rl_variable(lua_State* state);
    int             is_rl_variable_true(lua_State* state);
    int             get_host_process(lua_State* state);
    int             get_cwd(lua_State* state);
    int             get_console_aliases(lua_State* state);
    int             get_screen_info(lua_State* state);
};
