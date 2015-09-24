// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "match_generator_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_root.h>
#include <lua/lua_script_loader.h>

//------------------------------------------------------------------------------
struct dir_test
{
    typedef match_generator_tester<dir_test> tester;

                            dir_test();
                            ~dir_test();
                            operator match_generator* () { return m_generator; }
    lua_match_generator*    m_generator;
    lua_root                m_lua_root;
};

dir_test::dir_test()
{
    lua_State* state = m_lua_root.get_state();
    m_generator = new lua_match_generator(state);
    lua_load_script(state, dll, dir);
}

dir_test::~dir_test()
{
    delete m_generator;
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
                dir_test::tester(cmd, "t", "two_dir\\", "three_dir\\", nullptr);
            }

            SECTION("Single (with -/_) #1") {
                cmd << "two_d";
                dir_test::tester(cmd, "two_dir\\", nullptr);
            }

            SECTION("Single (with -/_) #2") {
                str_compare_scope _(str_compare_scope::relaxed);

                cmd << "one-";
                dir_test::tester(cmd, "one_dir\\", nullptr);
            }

            SECTION("Relative") {
                cmd << "nest_1\\..\\o";
                dir_test::tester(cmd, "nest_1\\..\\one_dir\\", nullptr);
            }

            SECTION("No matches") {
                cmd << "f";
                dir_test::tester(cmd, nullptr);
            }

            SECTION("Nested (forward slash)") {
                cmd << "nest_1/ne";
                dir_test::tester(cmd, "nest_1\\nest_2\\", nullptr);
            }
        }
    }
}
