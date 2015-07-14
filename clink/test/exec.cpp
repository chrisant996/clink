// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

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
