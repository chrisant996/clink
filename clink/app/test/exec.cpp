// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "env_fixture.h"
#include "line_editor_tester.h"

#include <core/path.h>
#include <core/settings.h>
#include <core/str_compare.h>
#include <lib/line_editor.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
TEST_CASE("Executable match generation.") {
    static const char* path_fs_desc[] = {
        "_path/spa ce.exe",
        "_path/one_path.exe",
        "_path/one_two.py",
        "_path/one_three.txt"
    };

    static const char* exec_fs_desc[] = {
        "one_dir/spa ce.exe",
        "one_dir/two_dir_local.exe",
        "one_dir/two_dir_local.txt",
        "foodir/two_dir_local.exe",
        "jumble/three.exe",
        "jumble/three-local.py",
        "one_local.exe",
        "two_local.exe",
        "one_local.txt",
        nullptr,
    };

	fs_fixture path_fs(path_fs_desc);
	fs_fixture exec_fs(exec_fs_desc);

    str<260> path_env_var(path_fs.get_root());
    path::append(path_env_var, "_path");
    const char* exec_env[] = {
        "path",     path_env_var.c_str(),
        "pathext",  ".exe;.py",
        nullptr,
    };
    env_fixture env(exec_env);

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua_load_script(lua, app, exec);

    line_editor_tester tester;
    tester.get_editor()->add_generator(lua_generator);

    settings::find("exec.cwd")->set("0");
    settings::find("exec.dirs")->set("0");

    SECTION("Nothing") {
        tester.set_input("abc123");
        tester.set_expected_matches();
        tester.run();
    }

    SECTION("PATH single") {
        tester.set_input("one_p");
        tester.set_expected_matches("one_path.exe");
        tester.run();
    }

    SECTION("PATH case mapped") {
        str_compare_scope _(str_compare_scope::relaxed);

        tester.set_input("one-p");
        tester.set_expected_matches("one_path.exe");
        tester.run();
    }

    SECTION("PATH matches") {
        tester.set_input("one_");
        tester.set_expected_matches("one_path.exe", "one_two.py");
        tester.run();
    }

#if MODE4 // tester's shell != cmd.exe
    SECTION("cmd.exe commands") {
        tester.set_input("p");
        tester.set_expected_matches("path", "pause", "popd", "prompt", "pushd");
        tester.run();
    }
#endif

    SECTION("Relative path") {
        tester.set_input(".\\");
        tester.set_expected_matches(
			"one_local.exe", "two_local.exe",
            "one_dir\\", "foodir\\", "jumble\\"
		);
        tester.run();
    }

#if MODE4
    clink.test.test_output(
        "relative path dir",
        ".\\foodir\\",
        ".\\foodir\\two_dir_local.exe "
    )

    clink.test.test_output(
        "relative path dir (with '_')",
        ".\\one_dir\\t",
        ".\\one_dir\\two_dir_local.exe "
    )

    clink.test.test_output(
        "separator | 1",
        "nullcmd | one_p",
        "nullcmd | one_path.exe "
    )

    clink.test.test_output(
        "separator | 2",
        "nullcmd |one_p",
        "nullcmd |one_path.exe "
    )

    clink.test.test_matches(
        "separator & 1",
        "nullcmd & one_",
        { "one_path.exe", "one_two.py" }
    )

    clink.test.test_matches(
        "separator & 2",
        "nullcmd &one_",
        { "one_path.exe", "one_two.py" }
    )

    clink.test.test_output(
        "separator && 1",
        "nullcmd && one_p",
        "nullcmd && one_path.exe "
    )

    clink.test.test_output(
        "separator && 2",
        "nullcmd &&one_p",
        "nullcmd &&one_path.exe "
    )

    clink.test.test_output(
        "spaces (path)",
        "spa",
        "\"spa ce.exe\" "
    )

    clink.test.test_output(
        "spaces (relative)",
        ".\\one_dir\\spa",
        "\".\\one_dir\\spa ce.exe\" "
    )

    clink.test.test_matches(
        "separator false positive",
        "nullcmd \"&&\" o\t",
        { "one_local.exe", "one_local.txt", "one_dir\\" }
    )

    clink.test.test_output(
        "last char . 1",
        "one_path.",
        "one_path.exe "
    )

    clink.test.test_output(
        "last char . 2",
        "jumble\\three.",
        "jumble\\three.exe "
    )

    clink.test.test_output(
        "last char -",
        "one_local-",
        "one_local-"
    )

    --------------------------------------------------------------------------------
    exec_match_style = 1
    clink.test.test_matches(
        "style - cwd (no dirs) 1",
        "one_",
        { "one_local.exe", "one_path.exe", "one_two.py" }
    )

    exec_match_style = 2
    clink.test.test_matches(
        "style - cwd (all)",
        "one-\t",
        { "one_local.exe", "one_path.exe", "one_two.py", "one_dir\\" }
    )

    --------------------------------------------------------------------------------
    exec_match_style = 0
    space_prefix_match_files = 1
    clink.test.test_matches(
        "space prefix; none",
        "one_",
        { "one_path.exe", "one_two.py" }
    )

    clink.test.test_matches(
        "space prefix; space",
        " one_",
        { "one_dir\\", "one_local.txt", "one_local.exe" }
    )

    clink.test.test_matches(
        "space prefix; spaces",
        "   one_",
        { "one_dir\\", "one_local.txt", "one_local.exe" }
    )
#endif // MODE4
}
