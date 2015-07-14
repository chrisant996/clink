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
#include "match_generator_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <lua/lua_root.h>
#include <lua/lua_script_loader.h>

//------------------------------------------------------------------------------
static const char* g_dir_fs[] = {
    "one_dir/leaf",
    "two_dir/leaf",
    "three_dir/leaf",
    "nest_1/nest_2/leaf",
    "one_file",
    "two_file",
    "three_file",
    "four_file",
    nullptr,
};

//------------------------------------------------------------------------------
class dir_lua_root : public lua_root {};

template <>
void match_generator_tester<dir_lua_root>::initialise()
{
    lua_State* state = m_generator.get_state();
    lua_load_script(state, dll, dir);
}

//------------------------------------------------------------------------------
TEST_CASE("Directory match generation.") {
    fs_fixture fs(g_dir_fs);

    const char* dir_cmds[] = { "cd", "rd", "rmdir", "md", "mkdir", "pushd" };
    for (int i = 0; i < sizeof_array(dir_cmds); ++i)
    {
        const char* dir_cmd = dir_cmds[i];

        str<> cmd;
        cmd << dir_cmd << " ";

        SECTION(dir_cmd) {
            SECTION("Matches") {
                match_generator_tester<dir_lua_root>(
                    (cmd << "t").c_str(),
                    "t", "two_dir\\", "three_dir\\", nullptr
                );
            }

            SECTION("Single (with -/_)") {
                match_generator_tester<dir_lua_root>(
                    (cmd << "one-").c_str(),
                    "one_dir\\ ", nullptr
                );
            }

            SECTION("Relative") {
                match_generator_tester<dir_lua_root>(
                    (cmd << "o\\..\\o").c_str(),
                    "o\\..\\one_dir\\ ", nullptr
                );
            }

            SECTION("No matches") {
                match_generator_tester<dir_lua_root>(
                    (cmd << "f").c_str(),
                    "f", nullptr
                );
            }

            SECTION("Nested (forward slash)") {
                match_generator_tester<dir_lua_root>(
                    (cmd << "nest_1/ne").c_str(),
                    "nest_1\\nest_2\\ ", nullptr
                );
            }
        }
    }
}
