// Copyright (c) 2013 Martin Ridgers
// Portions Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "line_editor_tester.h"

#include <core/path.h>
#include <core/settings.h>
#include <core/os.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
};

//------------------------------------------------------------------------------
TEST_CASE("Merge argmatchers")
{
    lua_state lua;
    lua_match_generator lua_generator(lua); // This loads the required lua scripts.

    line_editor::desc desc(nullptr, nullptr, nullptr, nullptr);
    line_editor_tester tester(desc, "&|", nullptr);
    tester.get_editor()->set_generator(lua_generator);

    SECTION("Main")
    {
        const char* script = "\
            local q = clink.argmatcher():addarg({ 'sub_p' }) \
            local p = clink.argmatcher('argcmd_merge') \
            p:addflags('-flg_p', '-flg_ps') \
            p:addarg({ \
                'sub_p______' .. q, \
                'sub_p_sub_s' .. q, \
                'sub_p_str_s' .. q, \
                'str_p______', \
                'str_p_sub_s', \
                'str_p_str_s', \
            }) \
            p:addarg({ \
                'str_p', \
            }) \
            \
            local r = clink.argmatcher():addarg({ 'sub_s' }) \
            local s = clink.argmatcher('argcmd_merge') \
            s:addflags('-flg_s', '-flg_ps') \
            s:addarg({ \
                '______sub_s' .. r, \
                'sub_p_sub_s' .. r, \
                'sub_p_str_s', \
                '______str_s', \
                'str_p_sub_s' .. r, \
                'str_p_str_s', \
            }) \
            s:addarg({ \
                'str_s', \
            }) \
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        struct merge_test
        {
            const char* input;
            const char* expected[10];
        };

        str<> input;
        auto run_test = [&](const merge_test& test) {
            input.format("argcmd_merge %s", test.input);
            tester.set_input(input.c_str());
            tester.set_expected_matches_list(&test.expected[0]);
            tester.run();
        };

        SECTION("Flags")
        {
            static const merge_test c_tests[] =
            {
                { "-flg_", { "-flg_p", "-flg_s", "-flg_ps" } },
            };

            for (const auto test : c_tests)
                run_test(test);
        }

        SECTION("Simple")
        {
            static const merge_test c_tests[] =
            {
                { "______str_s ", { "str_p", "str_s" } },
                { "______sub_s ", {          "sub_s" } },
                { "str_p______ ", { "str_p", "str_s" } },
                { "sub_p______ ", { "sub_p", "sub_s" } },   // Merges them because the arglinks use the same parser.
            };

            for (const auto test : c_tests)
                run_test(test);
        }

        SECTION("Linked")
        {
            static const merge_test c_tests[] =
            {
                { "str_p_str_s ", { "str_p", "str_s" } },
                { "str_p_sub_s ", {          "sub_s" } },
                { "sub_p_str_s ", { "sub_p", "sub_s" } },   // Merges them because the arglinks use the same parser.
                { "sub_p_sub_s ", { "sub_p", "sub_s" } },
            };

            for (const auto test : c_tests)
                run_test(test);
        }
    }

    SECTION("Deprecated")
    {
        const char* script = "\
            local q = clink.arg.new_parser():set_arguments({ 'sub_p' }) \
            local p = clink.arg.new_parser() \
            p:set_flags('-flg_p', '-flg_ps') \
            p:set_arguments( \
                { \
                    'sub_p______' .. q, \
                    'sub_p_sub_s' .. q, \
                    'sub_p_str_s' .. q, \
                    'str_p______', \
                    'str_p_sub_s', \
                    'str_p_str_s', \
                }, \
                { 'str_p' } \
            ) \
            \
            local r = clink.arg.new_parser():set_arguments({ 'sub_s' }) \
            local s = clink.arg.new_parser() \
            s:set_flags('-flg_s', '-flg_ps') \
            s:set_arguments( \
                { \
                    '______sub_s' .. r, \
                    'sub_p_sub_s' .. r, \
                    'sub_p_str_s', \
                    '______str_s', \
                    'str_p_sub_s' .. r, \
                    'str_p_str_s', \
                }, \
                { 'str_s' } \
            ) \
            \
            clink.arg.register_parser('argcmd_merge', p) \
            clink.arg.register_parser('argcmd_merge', s) \
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        struct merge_test
        {
            const char* input;
            const char* expected[10];
        };

        str<> input;
        auto run_test = [&](const merge_test& test) {
            input.format("argcmd_merge %s", test.input);
            tester.set_input(input.c_str());
            tester.set_expected_matches_list(&test.expected[0]);
            tester.run();
        };

        SECTION("Flags")
        {
            static const merge_test c_tests[] =
            {
                { "-flg_", { "-flg_p", "-flg_s", "-flg_ps" } },
            };

            for (const auto test : c_tests)
                run_test(test);
        }

        SECTION("Simple")
        {
            static const merge_test c_tests[] =
            {
                { "______str_s ", { "str_p"          } },   // Not ideal (has str_p, missing str_s).
                { "______sub_s ", {          "sub_s" } },
                { "str_p______ ", { "str_p"          } },
                { "sub_p______ ", { "sub_p", "sub_s" } },   // Merges them because the arglinks use the same parser.
            };

            for (const auto test : c_tests)
                run_test(test);
        }

        SECTION("Linked")
        {
            static const merge_test c_tests[] =
            {
                { "str_p_str_s ", { "str_p"          } },   // Not ideal (missing str_s).
                { "str_p_sub_s ", {          "sub_s" } },   // Not ideal (missing str_p).
                { "sub_p_str_s ", { "sub_p", "sub_s" } },   // Not ideal (has sub_s, missing str_s).
                { "sub_p_sub_s ", { "sub_p", "sub_s" } },
            };

            for (const auto test : c_tests)
                run_test(test);
        }
    }
}
