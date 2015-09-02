// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "match_generator_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <lua/lua_root.h>
#include <lua/lua_script_loader.h>

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
    static const char* dir_fs[] = {
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

    fs_fixture fs(dir_fs);

    const char* dir_cmds[] = { "cd", "rd", "rmdir", "md", "mkdir", "pushd" };
    for (int i = 0; i < sizeof_array(dir_cmds); ++i)
    {
        const char* dir_cmd = dir_cmds[i];

        str<> cmd;
        cmd << dir_cmd << " ";

        SECTION(dir_cmd) {
            SECTION("Matches") {
                cmd << "t";
                match_generator_tester<dir_lua_root>(cmd, "t",
                    "two_dir\\", "three_dir\\", nullptr);
            }

            SECTION("Single (with -/_) #1") {
                cmd << "two_d";
                match_generator_tester<dir_lua_root>(cmd, "two_dir\\", nullptr);
            }

            SECTION("Single (with -/_) #2") {
                str_compare_scope _(str_compare_scope::relaxed);

                cmd << "one-";
                match_generator_tester<dir_lua_root>(cmd, "one_dir\\", nullptr);
            }

            SECTION("Relative") {
                cmd << "nest_1\\..\\o";
                match_generator_tester<dir_lua_root>(cmd, "nest_1\\..\\one_dir\\", nullptr);
            }

            SECTION("No matches") {
                cmd << "f";
                match_generator_tester<dir_lua_root>(cmd, nullptr);
            }

            SECTION("Nested (forward slash)") {
                cmd << "nest_1/ne";
                match_generator_tester<dir_lua_root>(cmd, "nest_1\\nest_2\\", nullptr);
            }
        }
    }
}
