// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "env_fixture.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/str_compare.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
TEST_CASE("Set command.")
{
    static const char* env_desc[] = {
        "simple",   "value",
        "case_map", "value",
        "dash-1",   "value",
        "dash_2",   "value",
        nullptr
    };

    fs_fixture fs;
    env_fixture env(env_desc);

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua_load_script(lua, app, set);

    line_editor::desc desc(nullptr, nullptr, nullptr, nullptr);
    line_editor_tester tester(desc, nullptr, " =");
    tester.get_editor()->set_generator(lua_generator);

    SECTION("Matches")
    {
        tester.set_input("set simp");
        tester.set_expected_matches("simple");
        tester.run();
    }

    SECTION("Suffixed output")
    {
        tester.set_input("set simp" DO_COMPLETE);
        tester.set_expected_output("set simple=");
        tester.run();
    }

    SECTION("Case mapped")
    {
        str_compare_scope _(str_compare_scope::relaxed, false);

        SECTION("One match")
        {
            tester.set_input("set case_m");
            tester.set_expected_matches("case_map");
            tester.run();
        }

        SECTION("Many matches")
        {
            tester.set_input("set dash-");
            tester.set_expected_matches("dash-1", "dash_2");
            tester.run();
        }
    }

    SECTION("File matching after '='")
    {
        SECTION("*")
        {
            tester.set_input("set simple=");
            tester.set_expected_matches("file1", "file2", "case_map-1", "case_map_2",
                "dir1\\", "dir2\\");
            tester.run();
        }

        SECTION("dir*")
        {
            tester.set_input("set simple=dir");
            tester.set_expected_matches("dir1\\", "dir2\\");
            tester.run();
        }

        SECTION("file*")
        {
            tester.set_input("set simple=dir1\\file");
            tester.set_expected_matches("dir1\\file1", "dir1\\file2");
            tester.run();
        }
    }
}
