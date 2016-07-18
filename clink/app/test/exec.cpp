// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "env_fixture.h"

#if MODE4

#include <core/path.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_root.h>
#include <lua/lua_script_loader.h>

//------------------------------------------------------------------------------
struct exec_test
{
    typedef match_generator_tester<exec_test> tester;

                            exec_test();
                            ~exec_test();
                            operator match_generator& () { return *m_generator; }
    lua_match_generator*    m_generator;
    lua_root                m_lua_root;
};

exec_test::exec_test()
{
    lua_State* state = m_lua_root.get_state();
    m_generator = new lua_match_generator(state);
    lua_load_script(state, dll, exec);
}

exec_test::~exec_test()
{
    delete m_generator;
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

    str<260> path_path(fs.get_root());
    path::append(path_path, "_path");
    const char* exec_env[] = {
        "path",     path_path.c_str(),
        "pathext",  ".exe;.py",
        nullptr,
    };
    env_fixture env(exec_env);

#if MODE4
    clink.test.test_matches(
        "Nothing",
        "abc123",
        {}
    )
#endif

    SECTION("PATH") {
        exec_test::tester("one_p", "one_path.exe", nullptr);
    }

#if MODE4
    clink.test.test_output(
        "path case mapped",
        "one-p",
        "one_path.exe "
    )

    clink.test.test_matches(
        "path matches",
        "one_",
        { "one_path.exe", "one_two.py" }
    )

    clink.test.test_matches(
        "cmd.exe commands",
        "p",
        { "path", "pause", "popd", "prompt", "pushd" }
    )

    clink.test.test_matches(
        "relative path",
        ".\\",
        { "one_local.exe", "two_local.exe", "one_dir\\", "foodir\\", "jumble\\" }
    )

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

#endif // MODE4
