// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
TEST_CASE("Directory match generation.")
{
    static const char* dir_fs[] = {
        "one_dir/leaf",
        "two_dir/leaf",
        "three_dir/leaf",
        "nest_1/nest_2/leaf",
        "nest_1/nest_2/nest_3a/leaf",
        "nest_1/nest_2/nest_3b/leaf",
        "one_file",
        "two_file",
        "three_file",
        "four_file",
        nullptr,
    };

    fs_fixture fs(dir_fs);

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua_load_script(lua, app, core);
    lua_load_script(lua, app, dir);

    line_editor_tester tester;
    tester.get_editor()->add_generator(lua_generator);
    tester.get_editor()->add_generator(file_match_generator());

    const char* dir_cmds[] = { "cd", "rd", "rmdir", "md", "mkdir", "pushd" };
    for (int i = 0; i < sizeof_array(dir_cmds); ++i)
    {
        const char* dir_cmd = dir_cmds[i];

        str<> cmd;
        cmd << dir_cmd << " ";

        SECTION(dir_cmd)
        {
            SECTION("Matches")
            {
                cmd << "t";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches("two_dir\\", "three_dir\\");
                tester.run();
            }

            SECTION("Single (with -/_) 1")
            {
                cmd << "two_d";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches("two_dir\\");
                tester.run();
            }

            SECTION("Single (with -/_) 2")
            {
                str_compare_scope _(str_compare_scope::relaxed);

                cmd << "one-";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches("one_dir\\");
                tester.run();
            }

            SECTION("Single 3")
            {
                cmd << "one_dir";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches("one_dir\\");
                tester.run();
            }

            SECTION("Relative")
            {
                cmd << "nest_1\\..\\o";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches("nest_1\\..\\one_dir\\");
                tester.run();
            }

            SECTION("No matches")
            {
                cmd << "f";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches();
                tester.run();
            }

            SECTION("Nested 1")
            {
                cmd << "nest_1/ne";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches("nest_1\\nest_2\\");
                tester.run();
            }

            SECTION("Nested 2")
            {
                cmd << "nest_1/nest_2\\";
                tester.set_input(cmd.c_str());
                tester.set_expected_matches("nest_1/nest_2\\nest_3a\\", "nest_1/nest_2\\nest_3b\\");
                tester.run();
            }
        }
    }
}
