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

#include "catch.hpp"
#include "fs_fixture.h"
#include "env_fixture.h"
#include "match_generator_tester.h"

#include <core/path.h>
#include <lua/lua_root.h>
#include <lua/lua_script_loader.h>

//------------------------------------------------------------------------------
class exec_lua_root : public lua_root {};

template <>
void match_generator_tester<exec_lua_root>::initialise()
{
    lua_State* state = m_generator.get_state();
    lua_load_script(state, dll, exec);
}

//------------------------------------------------------------------------------
TEST_CASE("Executable match generation.") {
    static const char* exec_fs[] = {
        "_path/spa ce.exe",
        "_path/one_path.exe",
        "_path/one_two.py",
        "_path/one_three.txt"
        "one_dir/spa ce.exe",
        "one_dir/two_dir_local.exe",
        "one_dir/two_dir_local.txt"
        "foodir/two_dir_local.exe",
        "one_local.exe",
        "two_local.exe",
        "one_local.txt",
        nullptr,
    };
    fs_fixture fs(exec_fs);

    str<260> path_path;
    path::join(fs.get_root(), "_path", path_path);
    const char* exec_env[] = {
        "path",     path_path.c_str(),
        "pathext",  ".exe;.py",
        nullptr,
    };
    env_fixture env(exec_env);

    /*
    SECTION("PATH") {
        match_generator_tester<exec_lua_root>("one_p", "one_path.exe", nullptr);
    }
    */
}
