// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <readline/readline.h>

//------------------------------------------------------------------------------
TEST_CASE("Slash translation")
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
    lua_load_script(lua, app, dir);

    line_editor_tester tester;
    tester.get_editor()->set_generator(lua_generator);

    setting* setting = settings::find("match.translate_slashes");
    setting->set("off");

    tester.set_input("foo nest_1/nest_2\\");
    tester.set_expected_matches("nest_1/nest_2\\nest_3a\\", "nest_1/nest_2\\nest_3b\\", "nest_1/nest_2\\leaf");
    tester.run();

    tester.set_input("foo nest_1/nest_2\\nest_3a/");
    tester.set_expected_matches("nest_1/nest_2\\nest_3a/leaf");
    tester.run();

    tester.set_input("foo nest_1\\nest_2/nest_3a\\");
    tester.set_expected_matches("nest_1\\nest_2/nest_3a\\leaf");
    tester.run();

    setting->set("system");

    tester.set_input("foo nest_1/nest_2/");
    tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\", "nest_1\\nest_2\\nest_3b\\", "nest_1\\nest_2\\leaf");
    tester.run();

    tester.set_input("foo nest_1/nest_2/nest_3a/");
    tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\leaf");
    tester.run();

    setting->set("slash");

    tester.set_input("foo nest_1\\nest_2\\");
    tester.set_expected_matches("nest_1/nest_2/nest_3a/", "nest_1/nest_2/nest_3b/", "nest_1/nest_2/leaf");
    tester.run();

    tester.set_input("foo nest_1\\nest_2\\nest_3a\\");
    tester.set_expected_matches("nest_1/nest_2/nest_3a/leaf");
    tester.run();

    setting->set("backslash");

    tester.set_input("foo nest_1/nest_2/");
    tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\", "nest_1\\nest_2\\nest_3b\\", "nest_1\\nest_2\\leaf");
    tester.run();

    tester.set_input("foo nest_1/nest_2/nest_3a/");
    tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\leaf");
    tester.run();

    setting->set("auto");

    tester.set_input("foo nest_1/nest_2\\");
    tester.set_expected_matches("nest_1/nest_2/nest_3a/", "nest_1/nest_2/nest_3b/", "nest_1/nest_2/leaf");
    tester.run();

    tester.set_input("foo nest_1/nest_2\\nest_3a\\");
    tester.set_expected_matches("nest_1/nest_2/nest_3a/leaf");
    tester.run();

    tester.set_input("foo nest_1\\nest_2/");
    tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\", "nest_1\\nest_2\\nest_3b\\", "nest_1\\nest_2\\leaf");
    tester.run();

    tester.set_input("foo nest_1\\nest_2/nest_3a/");
    tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\leaf");
    tester.run();

    setting->set();
}
