// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "env_fixture.h"
#include "match_generator_tester.h"

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
                            operator match_generator* () { return m_generator; }
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

    SECTION("PATH") {
        exec_test::tester("one_p", "one_path.exe", nullptr);
    }

    // MODE4 - missing tests
}
