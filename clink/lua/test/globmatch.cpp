// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"

#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

//------------------------------------------------------------------------------
static bool run_test(lua_state& lua, const char** data, const char** expected_recursion=nullptr)
{
    lua_State* state = lua.get_state();

    str<280> s;
    str<> tmp;
    const char* pattern = *(data++);

    s << "local expected = {";
    while (*data)
    {
        tmp = *(data++);
        for (uint32 i = tmp.length(); i--;)
            if (tmp.c_str()[i] == '/')
                tmp.data()[i] = '\\';
        s << "[=[" << tmp.c_str() << "]=],";
    }
    s << "}\n";

    if (expected_recursion)
    {
        s << "local expected_recursion = {";
        while (*expected_recursion)
            s << "[=[" << *(expected_recursion++) << "]=],";
        s << "}\n";
    }

    s << "local result = do_test([=[" << pattern << "]=], expected, expected_recursion)\n";
    s << "return result\n";

    save_stack_top ss(state);

    int32 err = luaL_loadbuffer(state, s.c_str(), s.length(), s.c_str());
    if (!err)
        err = lua.pcall(0, LUA_MULTRET);
    if (err)
    {
        if (const char* error = lua_tostring(state, -1))
        {
            puts("");
            puts("error:");
            puts(error);
        }
        return false;
    }

    if (!lua_isboolean(state, -1))
        return false;

    return lua_toboolean(state, -1);
}

//------------------------------------------------------------------------------
TEST_CASE("Lua globmatch")
{
    static const char* dir_fs[] = {
        // Extra level of indentation to make copy/paste easier into test
        // sections further below.
            "one_dir/abc.txt",
            "two_dir/def.cpp",
            "three_dir/xyz.txt",
            "nest_1/nest_2a/xyz.txt",
            "nest_1/nest_2a/nest_3a/abc.txt",
            "nest_1/nest_2a/nest_3b/def.cpp",
            "nest_1/nest_2b/abc.txt",
            "nest_1/nest_2b/nest_3a/def.cpp",
            "nest_1/nest_2b/nest_3b/abc.txt",
            "nest_1/nest_2c/def.cpp",
            "cubby_1/cubby_2/cubby_3/abc.txt",
            "cubby_1/cubby_2/cubby_3/def.cpp",
            "cubby_1/cubby_2/cubby_3/xyz.txt",
            "cubby_1/cubby_2/abc.txt",
            "cubby_1/cubby_2/def.cpp",
            "cubby_1/cubby_2/xyz.txt",
            "abc.txt",
            "def.cpp",
            "xyz.txt",
            nullptr,
    };

    fs_fixture fs(dir_fs);

    lua_state lua;
    str_moveable s;

    static const char* main_script = "\
        local function print_expected_actual(kind, expected, actual)\
            print('')\
            table.sort(expected)\
            table.sort(actual)\
            print('expected '..kind..':')\
            for _, f in ipairs(expected) do\
                print('', f)\
            end\
            print('actual '..kind..':')\
            for _, f in ipairs(actual) do\
                print('', f)\
            end\
        end\
        \
        local function compare_actual_expected(kind, expected, actual)\
            if not expected then\
                return true\
            end\
            local index = {}\
            for _, x in ipairs(expected) do\
                index[x] = true\
            end\
            for _, x in ipairs(actual) do\
                if not index[x] then\
                    print_expected_actual(kind, expected, actual)\
                    return false\
                end\
                index[x] = nil\
            end\
            for x, _ in pairs(index) do\
                print_expected_actual(kind, expected, actual)\
                return false\
            end\
            return true\
        end\
        \
        function do_test(pattern, expected_matches, expected_recursion)\
            local flags = expected_recursion and {diagnostics=true}\
            local files, recursed = os.globmatch(pattern, flags)\
            return compare_actual_expected('matches', expected_matches, files) and\
                compare_actual_expected('recursion', expected_recursion, recursed)\
        end\
    ";

    REQUIRE(lua.do_string(main_script));

    SECTION("Simple")
    {
        static const char* data[] = {
            "**/*.txt",
            "one_dir/abc.txt",
            "three_dir/xyz.txt",
            "nest_1/nest_2a/xyz.txt",
            "nest_1/nest_2a/nest_3a/abc.txt",
            "nest_1/nest_2b/abc.txt",
            "nest_1/nest_2b/nest_3b/abc.txt",
            "cubby_1/cubby_2/cubby_3/abc.txt",
            "cubby_1/cubby_2/cubby_3/xyz.txt",
            "cubby_1/cubby_2/abc.txt",
            "cubby_1/cubby_2/xyz.txt",
            "abc.txt",
            "xyz.txt",
            nullptr
        };

        REQUIRE(run_test(lua, data));
    }

    SECTION("Dirs")
    {
        static const char* data[] = {
            "**/*b/",
            "nest_1/nest_2a/nest_3b/",
            "nest_1/nest_2b/",
            "nest_1/nest_2b/nest_3b/",
            nullptr
        };

        REQUIRE(run_test(lua, data));
    }

    SECTION("Wild nonwild wild")
    {
        static const char* data[] = {
            "*_[[:digit:]]/cubby_2/*x*",
            "cubby_1/cubby_2/abc.txt",
            "cubby_1/cubby_2/xyz.txt",
            nullptr
        };

        static const char* expected_recursion[] = {
            // Ideally it would only recurse into nest_1/ and cubby_1/, but
            // that would require specialized parsing logic.  The important
            // part is that it doesn't recurse into nest_1/nest_2a/ or
            // cubby_1/cubby_2/cubby_3/.
            "one_dir/",
            "two_dir/",
            "three_dir/",
            "nest_1/",
            "cubby_1/",
            nullptr
        };

        REQUIRE(run_test(lua, data, expected_recursion));
    }

}
