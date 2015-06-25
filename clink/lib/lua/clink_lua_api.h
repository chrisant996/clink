/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

//------------------------------------------------------------------------------
class clink_lua_api
{
public:
                    clink_lua_api();
    void            initialise(struct lua_State* state);

protected:
    int             change_dir(lua_State* state);
    int             to_lowercase(lua_State* state);
    int             find_files_impl(lua_State* state, int dirs_only);
    int             find_files(lua_State* state);
    int             find_dirs(lua_State* state);
    int             matches_are_files(lua_State* state);
    int             get_env(lua_State* state);
    int             get_env_var_names(lua_State* state);
    int             get_setting_str(lua_State* state);
    int             get_setting_int(lua_State* state);
    int             suppress_char_append(lua_State* state);
    int             suppress_quoting(lua_State* state);
    int             slash_translation(lua_State* state);
    int             is_dir(lua_State* state);
    int             get_rl_variable(lua_State* state);
    int             is_rl_variable_true(lua_State* state);
    int             get_host_process(lua_State* state);
    int             get_cwd(lua_State* state);
    int             get_console_aliases(lua_State* state);
    int             get_screen_info(lua_State* state);
};
