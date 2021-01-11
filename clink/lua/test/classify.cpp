// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/path.h>
#include <core/settings.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_word_classifier.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
TEST_CASE("Lua word classification.")
{
    lua_state lua;
    lua_match_generator lua_generator(lua); // This loads the required lua scripts.
    lua_load_script(lua, app, cmd);
    lua_load_script(lua, app, dir);
    lua_word_classifier lua_classifier(lua);

    settings::find("clink.colorize_input")->set("true");

    line_editor::desc desc(nullptr, nullptr, nullptr);
    desc.command_delims = "&|";
    line_editor_tester tester(desc);
    tester.get_editor()->add_generator(lua_generator);
    tester.get_editor()->add_generator(file_match_generator());
    tester.get_editor()->set_classifier(lua_classifier);

    str<> host;
    {
        char module[280];
        GetModuleFileNameA(nullptr, module, sizeof_array(module));
        module[sizeof_array(module) - 1] = '\0';
        path::get_name(module, host);
    }

    AddConsoleAliasA("dkalias", "text", host.data());

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
            \
            x = clink.argmatcher('xyz'):addarg(\
                'abc',\
                'def',\
                'qq'..clink.argmatcher():addflags('-z'),\
                'nf'..clink.argmatcher():nofiles()\
            ):addflags('-a', '--bee', '-c'):nofiles()\
        ";

        REQUIRE(lua.do_string(script));

#if 0
        SECTION("Empty")
        {
            tester.set_input("");
            tester.set_expected_classifications("");
            tester.run();
        }
#endif

        SECTION("Command")
        {
            tester.set_input("echo");
            tester.set_expected_classifications("c");
            tester.run();
        }

        SECTION("Command args")
        {
            tester.set_input("cd /d asdf");
            tester.set_expected_classifications("cfo");
            tester.run();
        }

        SECTION("Doskey")
        {
            tester.set_input("dkalias");
            tester.set_expected_classifications("d");
            tester.run();
        }

        SECTION("No matcher")
        {
            tester.set_input("asdflkj");
            tester.set_expected_classifications("o");
            tester.run();
        }

        SECTION("No matcher etc")
        {
            tester.set_input("asdflkj etc");
            tester.set_expected_classifications("o"); // No matcher, so stops after command.
            tester.run();
        }

        SECTION("Deplete matchers")
        {
            tester.set_input("xyz abc def ghi");
            tester.set_expected_classifications("oann");
            tester.run();
        }

        SECTION("Space arg")
        {
            tester.set_input("argcmd \"spa ce\"");
            tester.set_expected_classifications("oa");
            tester.run();
        }

        SECTION("Space other")
        {
            tester.set_input("argcmd \"spa cey\"");
            tester.set_expected_classifications("oo");
            tester.run();
        }

        SECTION("Space arg other")
        {
            tester.set_input("argcmd \"spa ce\" zzz");
            tester.set_expected_classifications("oao");
            tester.run();
        }

        SECTION("Space arg arg")
        {
            tester.set_input("argcmd \"spa ce\" two");
            tester.set_expected_classifications("oaa");
            tester.run();
        }

        SECTION("Flags")
        {
            tester.set_input("xyz --bee abc zzz");
            tester.set_expected_classifications("ofan");
            tester.run();
        }

        SECTION("Flags not flag")
        {
            tester.set_input("xyz --bee qq -a");
            tester.set_expected_classifications("ofao");
            tester.run();
        }

        SECTION("Flags nofiles")
        {
            tester.set_input("xyz --bee nf -a");
            tester.set_expected_classifications("ofan");
            tester.run();
        }

        SECTION("Flags unrecognized flag")
        {
            // An unrecognized flag is not counted as an arg.
            tester.set_input("xyz --bee -unknown abc");
            tester.set_expected_classifications("ofoa");
            tester.run();
        }

        SECTION("Flags unrecognized arg")
        {
            tester.set_input("xyz --bee mno abc");
            tester.set_expected_classifications("ofon");
            tester.run();
        }

        SECTION("Flags not linked")
        {
            // Since 'abc' doesn't link to anything, xyz flags continue to be
            // accepted after it.
            tester.set_input("xyz --bee -a abc -a mno -c");
            tester.set_expected_classifications("offafnf");
            tester.run();
        }

        SECTION("Flags linked")
        {
            tester.set_input("xyz --bee qq -z");
            tester.set_expected_classifications("ofaf");
            tester.run();
        }

        SECTION("Node matches 3 (.exe)")
        {
            tester.set_input("argcmd.exe t");
            tester.set_expected_classifications("oo");
            tester.run();
        }

        SECTION("Node matches 4 (.bat)")
        {
            tester.set_input("argcmd.bat t");
            tester.set_expected_classifications("oo");
            tester.run();
        }

        SECTION("Node matches quoted 1")
        {
            tester.set_input("argcmd \"t");
            tester.set_expected_classifications("oo");
            tester.run();
        }

        SECTION("Node matches quoted executable")
        {
            tester.set_input("\"argcmd\" t");
            tester.set_expected_classifications("oo");
            tester.run();
        }

        SECTION("Key as only match.")
        {
            tester.set_input("argcmd three ");
            tester.set_expected_classifications("oa");
            tester.run();
        }

        SECTION("Simple traversal 1")
        {
            tester.set_input("argcmd three four ");
            tester.set_expected_classifications("oaa");
            tester.run();
        }

        SECTION("Simple traversal 2")
        {
            tester.set_input("argcmd three f");
            tester.set_expected_classifications("oao");
            tester.run();
        }

        SECTION("Simple traversal 3")
        {
            tester.set_input("argcmd one one one");
            tester.set_expected_classifications("oaoo");
            tester.run();
        }

        SECTION("Simple traversal 4")
        {
            tester.set_input("argcmd one one ");
            tester.set_expected_classifications("oao");
            tester.run();
        }

        SECTION("Quoted traversal 1")
        {
            tester.set_input("argcmd \"three\" four ");
            tester.set_expected_classifications("oaa");
            tester.run();
        }

        SECTION("Quoted traversal 2a")
        {
            tester.set_input("argcmd three four \"");
            tester.set_expected_classifications("oaao");
            tester.run();
        }

        SECTION("Quoted traversal 2b")
        {
            tester.set_input("argcmd three four \"fi");
            tester.set_expected_classifications("oaao");
            tester.run();
        }

        SECTION("Quoted traversal 2c")
        {
            tester.set_input("argcmd three four \"five\" five five s");
            tester.set_expected_classifications("oaaaaao");
            tester.run();
        }

        SECTION("Quoted traversal 3")
        {
            tester.set_input("argcmd \"three\" ");
            tester.set_expected_classifications("oa");
            tester.run();
        }

        SECTION("Quoted traversal 4")
        {
            tester.set_input("argcmd \"spa ce\" ");
            tester.set_expected_classifications("oa");
            tester.run();
        }

        SECTION("Quoted traversal 5")
        {
            tester.set_input("argcmd spa");
            tester.set_expected_classifications("oo");
            tester.run();
        }

        SECTION("Looping: basic")
        {
            tester.set_input("argcmd three four six ");
            tester.set_expected_classifications("oaaa");
            tester.run();
        }

        SECTION("Looping: miss")
        {
            tester.set_input("argcmd three four green four ");
            tester.set_expected_classifications("oaaoo");
            tester.run();
        }

        SECTION("Separator && 1")
        {
            tester.set_input("nullcmd && cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator && 1")
        {
            tester.set_input("nullcmd &&cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator && 2")
        {
            tester.set_input("nullcmd \"&&\" && cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator && 3")
        {
            tester.set_input("nullcmd \"&&\"&&cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator &")
        {
            tester.set_input("nullcmd & cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator | 1")
        {
            tester.set_input("nullcmd | cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator | 2")
        {
            tester.set_input("nullcmd|cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator multiple 1")
        {
            tester.set_input("nullcmd | cd && cd t");
            tester.set_expected_classifications("occo");
            tester.run();
        }

        SECTION("Separator multiple 2")
        {
            tester.set_input("nullcmd | cd && argcmd |cd t");
            tester.set_expected_classifications("ococo");
            tester.run();
        }

        SECTION("Multiple commands with args")
        {
            tester.set_input("xyz abc green | asdfjkl etc | echo etc | xyz -a def && argcmd t");
            tester.set_expected_classifications("oanocofaoo");
            tester.run();
        }

        SECTION("No separator")
        {
            tester.set_input("argcmd three four \"  &&foobar\" f");
            tester.set_expected_classifications("oaaoo");
            tester.run();
        }

        SECTION("Path: relative")
        {
            tester.set_input(".\\foo\\bar\\argcmd t");
            tester.set_expected_classifications("oo");
            tester.run();
        }

        SECTION("Path: absolute")
        {
            tester.set_input("c:\\foo\\bar\\argcmd t");
            tester.set_expected_classifications("oo");
            tester.run();
        }
    }

    SECTION("Doskey")
    {
        SECTION("No space")
        {
            tester.set_input("dkalias");
            tester.set_expected_classifications("d");
            tester.run();
        }

        SECTION("Space")
        {
            tester.set_input(" dkalias");
            tester.set_expected_classifications("o");
            tester.run();
        }
    }

#if 0
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
#endif

#if 0
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
#endif

#if 0
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
#endif

#if 0
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
#endif

#if 0
    SECTION("Flags")
    {
        const char* script = "\
            p = clink.argmatcher('argcmd_flags_s')\
            :addflags('/one', '/two', '/twenty')\
            \
            clink.argmatcher('argcmd_flags_d')\
            :addflags('-one', '-two', '-twenty')\
            \
            clink.argmatcher('argcmd_flags_x')\
            :addflags('-oa', '-ob', '-oc')\
            :addarg('-od', '-oe', '-of')\
            :setflagprefix()\
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
#endif

#if 0
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
#endif

#if 0
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
#endif

    AddConsoleAliasA("dkalias", nullptr, host.data());
}
