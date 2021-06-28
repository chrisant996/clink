// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
TEST_CASE("Lua arg parsers")
{
    fs_fixture fs;

    lua_state lua;
    lua_match_generator lua_generator(lua);

    line_editor::desc desc(nullptr, nullptr, nullptr);
    desc.command_delims = "&|";
    line_editor_tester tester(desc);
    tester.get_editor()->add_generator(lua_generator);
    tester.get_editor()->add_generator(file_match_generator());

    SECTION("Main")
    {
        const char* script = "\
            s = clink.argmatcher():addarg('one', 'two') \
            r = clink.argmatcher():addarg('five', 'six'):loop() \
            q = clink.argmatcher():addarg('four' .. r) \
            clink.argmatcher('argcmd'):addarg(\
                'one',\
                'two',\
                'three' .. q,\
                'spa ce' .. s\
            )\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Node matches 1")
        {
            tester.set_input("argcmd ");
            tester.set_expected_matches("one", "two", "three", "spa ce");
            tester.run();
        }

        SECTION("Node matches 2")
        {
            tester.set_input("argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches 3 (.exe)")
        {
            tester.set_input("argcmd.exe t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches 4 (.bat)")
        {
            tester.set_input("argcmd.bat t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches quoted 1")
        {
            tester.set_input("argcmd \"t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches quoted executable")
        {
            tester.set_input("\"argcmd\" t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Key as only match.")
        {
            tester.set_input("argcmd three ");
            tester.set_expected_matches("four");
            tester.run();
        }

        SECTION("Simple traversal 1")
        {
            tester.set_input("argcmd three four ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Simple traversal 2")
        {
            tester.set_input("argcmd three f");
            tester.set_expected_matches("four");
            tester.run();
        }

        SECTION("Simple traversal 3")
        {
            tester.set_input("argcmd one one one");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Simple traversal 4")
        {
            tester.set_input("argcmd one one ");
            tester.set_expected_matches("file1", "file2", "case_map-1", "case_map_2",
                "dir1\\", "dir2\\");
            tester.run();
        }

        SECTION("Quoted traversal 1")
        {
            tester.set_input("argcmd \"three\" four ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Quoted traversal 2a")
        {
            tester.set_input("argcmd three four \"");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Quoted traversal 2b")
        {
            tester.set_input("argcmd three four \"fi");
            tester.set_expected_matches("five");
            tester.run();
        }

        SECTION("Quoted traversal 2c")
        {
            tester.set_input("argcmd three four \"five\" five five s");
            tester.set_expected_matches("six");
            tester.run();
        }

        SECTION("Quoted traversal 3")
        {
            tester.set_input("argcmd \"three\" ");
            tester.set_expected_matches("four");
            tester.run();
        }

        SECTION("Quoted traversal 4")
        {
            tester.set_input("argcmd \"spa ce\" ");
            tester.set_expected_matches("one", "two");
            tester.run();
        }

        SECTION("Quoted traversal 5")
        {
            tester.set_input("argcmd spa");
            tester.set_expected_matches("spa ce");
            tester.run();
        }

        SECTION("Looping: basic")
        {
            tester.set_input("argcmd three four six ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Looping: miss")
        {
            tester.set_input("argcmd three four green four ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Separator && 1")
        {
            tester.set_input("nullcmd && argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator && 1")
        {
            tester.set_input("nullcmd &&argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator && 2")
        {
            tester.set_input("nullcmd \"&&\" && argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator && 3")
        {
            tester.set_input("nullcmd \"&&\"&&argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator &")
        {
            tester.set_input("nullcmd & argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator | 1")
        {
            tester.set_input("nullcmd | argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator | 2")
        {
            tester.set_input("nullcmd|argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator multiple 1")
        {
            tester.set_input("nullcmd | nullcmd && argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator multiple 2")
        {
            tester.set_input("nullcmd | nullcmd && argcmd |argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("No separator")
        {
            tester.set_input("argcmd three four \"  &&foobar\" f");
            tester.set_expected_matches("five");
            tester.run();
        }

        SECTION("Path: relative")
        {
            tester.set_input(".\\foo\\bar\\argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Path: absolute")
        {
            tester.set_input("c:\\foo\\bar\\argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }
    }

    SECTION("File matching control.")
    {
        const char* script = "\
            q = clink.argmatcher('argcmd_file_empty') \
            p = clink.argmatcher('argcmd_file')\
            :addarg(\
                'true',\
                'sub_parser_1' .. clink.argmatcher():nofiles(),\
                'sub_parser_2' .. clink.argmatcher(),\
                'this_parser'\
            )\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Enabled")
        {
            tester.set_input("argcmd_file true ");
            tester.set_expected_matches("file1", "file2", "case_map-1", "case_map_2",
                "dir1\\", "dir2\\");
            tester.run();
        }

        SECTION("Disabled: empty")
        {
            tester.set_input("argcmd_file_empty ");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Disabled: sub 1")
        {
            tester.set_input("argcmd_file sub_parser_1 ");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Disabled: sub 2")
        {
            tester.set_input("argcmd_file sub_parser_2 ");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Disabled: this")
        {
            lua.do_string("p:nofiles()");
            tester.set_input("argcmd_file this_parser ");
            tester.set_expected_matches();
            tester.run();
        }
    }

    SECTION("Tables 1")
    {
        const char* script = "\
            q = clink.argmatcher():addarg('four', 'five') \
            clink.argmatcher('argcmd_substr'):addarg(\
                { 'one', 'onetwo', 'onethree' } .. q\
            )\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Full match is also partial match 1")
        {
            tester.set_input("argcmd_substr one");
            tester.set_expected_matches("one", "onetwo", "onethree");
            tester.run();
        }

        SECTION("Full match is also partial match 2")
        {
            tester.set_input("argcmd_substr one f");
            tester.set_expected_matches("four", "five");
            tester.run();
        }
    }

    SECTION("Tables 2")
    {
        const char* script = "\
            local tbl_1 = { 'one', 'two', 'three' }\
            local tbl_2 = { 'four', 'five', tbl_1 }\
            q = clink.argmatcher():addarg('fifth', tbl_2) \
            clink.argmatcher('argcmd_nested'):addarg({'once', tbl_1 } .. q)\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Nested table: simple")
        {
            tester.set_input("argcmd_nested on");
            tester.set_expected_matches("once", "one");
            tester.run();
        }

        SECTION("Nested table: sub-parser")
        {
            tester.set_input("argcmd_nested once f");
            tester.set_expected_matches("fifth", "four", "five");
            tester.run();
        }
    }

    SECTION("Looping")
    {
        const char* script = "\
            clink.argmatcher('argcmd_parser')\
            :addarg('two', 'three')\
            :addarg('four', 'banana')\
            :loop()\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Not looped yet")
        {
            tester.set_input("argcmd_parser t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Looped once")
        {
            tester.set_input("argcmd_parser two four t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Looped twice")
        {
            tester.set_input("argcmd_parser two four abc ba");
            tester.set_expected_matches("banana");
            tester.run();
        }
    }

    SECTION("Flags")
    {
        const char* script = "\
            p = clink.argmatcher('argcmd_flags_s')\
            :addflags('/one', '/two', '/twenty')\
            \
            clink.argmatcher('argcmd_flags_d')\
            :addflags('-one', '-two', '-twenty')\
            \
            local parser =\
            clink.argmatcher('argcmd_flags_x')\
            :addflags('-oa', '-ob', '-oc')\
            :addarg('-od', '-oe', '-of')\
            parser._deprecated = true\
            parser:setflagprefix()\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Slash 1")
        {
            tester.set_input("argcmd_flags_s /");
            tester.set_expected_matches("/one", "/two", "/twenty");
            tester.run();
        }

        SECTION("Slash 2")
        {
            tester.set_input("argcmd_flags_s nothing /");
            tester.set_expected_matches("/one", "/two", "/twenty");
            tester.run();
        }

        SECTION("Slash 3")
        {
            tester.set_input("argcmd_flags_s nothing /tw");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        REQUIRE(lua.do_string("p:nofiles()"));

        SECTION("Slash 4")
        {
            tester.set_input("argcmd_flags_s /t");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Slash 5")
        {
            tester.set_input("argcmd_flags_s nothing /");
            tester.set_expected_matches("/one", "/two", "/twenty");
            tester.run();
        }

        SECTION("Slash 6")
        {
            tester.set_input("argcmd_flags_s nothing /tw");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Slash 7")
        {
            tester.set_input("argcmd_flags_s out of bounds /t");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Slash 8")
        {
            tester.set_input("argcmd_flags_s /tw");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Dash 1")
        {
            tester.set_input("argcmd_flags_d -");
            tester.set_expected_matches("-one", "-two", "-twenty");
            tester.run();
        }

        SECTION("Dash 2")
        {
            tester.set_input("argcmd_flags_d -tw");
            tester.set_expected_matches("-two", "-twenty");
            tester.run();
        }

        SECTION("Dash 3")
        {
            tester.set_input("argcmd_flags_d -twe" DO_COMPLETE);
            tester.set_expected_output("argcmd_flags_d -twenty ");
            tester.run();
        }

        SECTION("No prefix 1")
        {
            tester.set_input("argcmd_flags_x ");
            tester.set_expected_matches("-od", "-oe", "-of");
            tester.run();
        }

        SECTION("No prefix 2")
        {
            tester.set_input("argcmd_flags_x -");
            tester.set_expected_matches("-od", "-oe", "-of");
            tester.run();
        }

        SECTION("No prefix 3")
        {
            tester.set_input("argcmd_flags_x -o");
            tester.set_expected_matches("-od", "-oe", "-of");
            tester.run();
        }
    }

    SECTION("Skip")
    {
        const char* script = "\
            q = clink.argmatcher():addarg('two', 'three')\
            \
            p = clink.argmatcher('argcmd_skip')\
            :addarg('one')\
            :addarg('nine')\
            :addflags('-flag_a' .. q, '-flag_b' .. q)\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Skip 1")
        {
            tester.set_input("argcmd_skip one two -fla");
            tester.set_expected_matches("-flag_a", "-flag_b");
            tester.run();
        }

        SECTION("Skip 2")
        {
            tester.set_input("argcmd_skip one two -flag_a t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Skip 3")
        {
            tester.set_input("argcmd_skip one two -flag_a two four five -f");
            tester.set_expected_matches("-flag_a", "-flag_b");
            tester.run();
        }
    }

    SECTION("Shorthand")
    {
        const char* script = "\
            clink.argmatcher('argcmd_shorthand')\
                { 'one', 'two', 'three' }\
                { 'four', 'five' }\
                { '-flag' .. clink.argmatcher() { 'red', 'green', 'blue'} }\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("First word")
        {
            tester.set_input("argcmd_shorthand o");
            tester.set_expected_matches("one");
            tester.run();
        }

        SECTION("Second word 1")
        {
            tester.set_input("argcmd_shorthand one f");
            tester.set_expected_matches("four", "five");
            tester.run();
        }

        SECTION("Second word 2")
        {
            tester.set_input("argcmd_shorthand abc f");
            tester.set_expected_matches("four", "five");
            tester.run();
        }

        SECTION("Flag 1")
        {
            tester.set_input("argcmd_shorthand -flag ");
            tester.set_expected_matches("red", "green", "blue");
            tester.run();
        }

        SECTION("Flag 2")
        {
            tester.set_input("argcmd_shorthand -flag red t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Flag 3")
        {
            tester.set_input("argcmd_shorthand one four -flag ");
            tester.set_expected_matches("red", "green", "blue");
            tester.run();
        }

        SECTION("Flag 4")
        {
            tester.set_input("argcmd_shorthand abc -flag ");
            tester.set_expected_matches("red", "green", "blue");
            tester.run();
        }

        SECTION("Flag 5")
        {
            tester.set_input("argcmd_shorthand abc -flag X ");
            tester.set_expected_matches("four", "five");
            tester.run();
        }

        SECTION("Flag 6")
        {
            tester.set_input("argcmd_shorthand abc 1 2 3 -flag ");
            tester.set_expected_matches("red", "green", "blue");
            tester.run();
        }

        SECTION("Flag 7")
        {
            tester.set_input("argcmd_shorthand abc 1 2 3 -flag red -fla");
            tester.set_expected_matches("-flag");
            tester.run();
        }

        SECTION("Flag 8")
        {
            tester.set_input("argcmd_shorthand abc 1 2 3 -flag red -flag r");
            tester.set_expected_matches("red");
            tester.run();
        }
    }

    SECTION("Sort")
    {
        static const char* sort_fs[] = {
            "clink/foo",
            "clink.future/foo",
            "aardvark",
            "zebra",
            nullptr,
        };

        fs_fixture fs_sort(sort_fs);

        SECTION("Compare")
        {
            REQUIRE(stricmp("clink", "clink.future") < 0);
            REQUIRE(stricmp("clink\\", "clink.future\\") > 0);
            REQUIRE(wcsicmp(L"clink", L"clink.future") < 0);
            REQUIRE(wcsicmp(L"clink\\", L"clink.future\\") > 0);
        }

        SECTION("Dir")
        {
            tester.set_input("echo \x1b*");
            tester.set_expected_output("echo aardvark clink\\ clink.future\\ zebra ");
            tester.run();
        }
    }
}
