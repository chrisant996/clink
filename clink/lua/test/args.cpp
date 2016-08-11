// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>


//------------------------------------------------------------------------------
TEST_CASE("Lua arg parsers.") {
    fs_fixture fs;

    lua_state lua;
    lua_match_generator lua_generator(lua);

    line_editor::desc desc;
    desc.command_delims = "&|";
    line_editor_tester tester(desc);
    tester.get_editor()->add_generator(lua_generator);
    tester.get_editor()->add_generator(file_match_generator());

    SECTION("Main") {
        const char* script = "\
            s = clink.arg.new_parser()\
            s:set_arguments({ 'one' , 'two' })\
            \
            r = clink.arg.new_parser()\
            r:set_arguments({ 'five', 'six' })\
            r:loop()\
            \
            q = clink.arg.new_parser()\
            q:set_arguments({ 'four' .. r })\
            \
            p = clink.arg.new_parser()\
            p:set_arguments(\
                {\
                    'one',\
                    'two',\
                    'three' .. q,\
                    'spa ce' .. s,\
                }\
            )\
            \
            clink.arg.register_parser('argcmd', p)\
        ";

        //__debugbreak();
        REQUIRE(lua.do_string(script));

        SECTION("Node matches 1") {
            tester.set_input("argcmd ");
            tester.set_expected_matches("one", "two", "three", "spa ce");
            tester.run();
        }

        SECTION("Node matches 2") {
            tester.set_input("argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches 3 (.exe)") {
            tester.set_input("argcmd.exe t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches 4 (.bat)") {
            tester.set_input("argcmd.bat t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches quoted 1") {
            tester.set_input("argcmd \"t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Node matches quoted executable") {
            tester.set_input("\"argcmd\" t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Key as only match.") {
            tester.set_input("argcmd three ");
            tester.set_expected_matches("four");
            tester.run();
        }

        SECTION("Simple traversal 1") {
            tester.set_input("argcmd three four ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Simple traversal 2") {
            tester.set_input("argcmd three f");
            tester.set_expected_matches("four");
            tester.run();
        }

        SECTION("Simple traversal 3") {
            tester.set_input("argcmd one one one");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Simple traversal 4") {
            tester.set_input("argcmd one one ");
            tester.set_expected_matches("file1", "file2", "case_map-1", "case_map_2",
                "dir1\\", "dir2\\");
            tester.run();
        }

        SECTION("Quoted traversal 1") {
            tester.set_input("argcmd \"three\" four ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Quoted traversal 2a") {
            tester.set_input("argcmd three four \"");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Quoted traversal 2b") {
            tester.set_input("argcmd three four \"fi");
            tester.set_expected_matches("five");
            tester.run();
        }

        SECTION("Quoted traversal 2c") {
            tester.set_input("argcmd three four \"five\" five five s");
            tester.set_expected_matches("six");
            tester.run();
        }

        SECTION("Quoted traversal 3") {
            tester.set_input("argcmd \"three\" ");
            tester.set_expected_matches("four");
            tester.run();
        }

        SECTION("Quoted traversal 4") {
            tester.set_input("argcmd \"spa ce\" ");
            tester.set_expected_matches("one", "two");
            tester.run();
        }

        SECTION("Quoted traversal 5") {
            tester.set_input("argcmd spa");
            tester.set_expected_matches("spa ce");
            tester.run();
        }

        SECTION("Loop property: basic") {
            tester.set_input("argcmd three four six ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Loop property: miss") {
            tester.set_input("argcmd three four green four ");
            tester.set_expected_matches("five", "six");
            tester.run();
        }

        SECTION("Separator && 1") {
            tester.set_input("nullcmd && argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator && 1") {
            tester.set_input("nullcmd &&argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator && 2") {
            tester.set_input("nullcmd \"&&\" && argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator && 3") {
            tester.set_input("nullcmd \"&&\"&&argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator &") {
            tester.set_input("nullcmd & argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator | 1") {
            tester.set_input("nullcmd | argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator | 2") {
            tester.set_input("nullcmd|argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator multiple 1") {
            tester.set_input("nullcmd | nullcmd && argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Separator multiple 2") {
            tester.set_input("nullcmd | nullcmd && argcmd |argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("No separator") {
            tester.set_input("argcmd three four \"  &&foobar\" f");
            tester.set_expected_matches("five");
            tester.run();
        }

        SECTION("Path: relative") {
            tester.set_input(".\\foo\\bar\\argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

        SECTION("Path: absolute") {
            tester.set_input("c:\\foo\\bar\\argcmd t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }
    }

    SECTION("File matching control.") {
        const char* script = "\
            p = clink.arg.new_parser()\
            p:set_arguments({\
                'true',\
                'sub_parser' .. clink.arg.new_parser():disable_file_matching(),\
                'this_parser'\
            })\
            \
            clink.arg.register_parser('argcmd_file', p)\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Enabled") {
            tester.set_input("argcmd_file true ");
            tester.set_expected_matches("file1", "file2", "case_map-1", "case_map_2",
                "dir1\\", "dir2\\");
            tester.run();
        }

        SECTION("Disabled: sub") {
            tester.set_input("argcmd_file sub_parser ");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Disabled: this") {
            lua.do_string("p:disable_file_matching()");
            tester.set_input("argcmd_file this_parser ");
            tester.set_expected_matches();
            tester.run();
        }
    }

    SECTION("Tables : parserless") {
        const char* script = "\
            clink.arg.register_parser('argcmd_table', {'two', 'three', 'one'});\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Parserless: table") {
            tester.set_input("argcmd_table t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }
    }

    SECTION("Tables 1") {
        const char* script = "\
            q = clink.arg.new_parser()\
            q:set_arguments({ 'four', 'five' })\
            \
            p = clink.arg.new_parser()\
            p:set_arguments(\
                { 'one', 'onetwo', 'onethree' } .. q\
            )\
            \
            clink.arg.register_parser('argcmd_substr', p);\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Full match is also partial match 1") {
            tester.set_input("argcmd_substr one");
            tester.set_expected_matches("one", "onetwo", "onethree");
            tester.run();
        }

        SECTION("Full match is also partial match 2") {
            tester.set_input("argcmd_substr one f");
            tester.set_expected_matches("four", "five");
            tester.run();
        }
    }

    SECTION("Tables 2") {
        const char* script = "\
            local tbl_1 = { 'one', 'two', 'three' }\
            local tbl_2 = { 'four', 'five', tbl_1 }\
            \
            q = clink.arg.new_parser()\
            q:set_arguments({ 'fifth', tbl_2 })\
            \
            p = clink.arg.new_parser()\
            p:set_arguments({ 'once', tbl_1 } .. q)\
            \
            clink.arg.register_parser('argcmd_nested', p)\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Nested table: simple") {
            tester.set_input("argcmd_nested on");
            tester.set_expected_matches("once", "one");
            tester.run();
        }

        SECTION("Nested table: sub-parser") {
            tester.set_input("argcmd_nested once f");
            tester.set_expected_matches("fifth", "four", "five");
            tester.run();
        }
    }

    SECTION("Looping") {
        const char* script = "\
            q = clink.arg.new_parser()\
            q:set_arguments({ 'two', 'three' }, { 'four', 'banana' })\
            q:loop()\
            \
            p = clink.arg.new_parser()\
            p:set_arguments({ 'one' }, q)\
            \
            clink.arg.register_parser('argcmd_parser', p)\
        ";

        REQUIRE(lua.do_string(script));
    
        SECTION("Nested full parser") {
            tester.set_input("argcmd_parser one t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }
    
        SECTION("Nested full parser - loop") {
            tester.set_input("argcmd_parser one two four t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }
    }

#if MODE4 // flag-type arguments don't work properly yet.
    SECTION("Flags") {
        const char* script = "\
            p = clink.arg.new_parser()\
            p:set_flags('/one', '/two', '/twenty')\
            \
            q = clink.arg.new_parser()\
            q:set_flags('-one', '-two', '-twenty')\
            \
            clink.arg.register_parser('argcmd_flags_s', p)\
            clink.arg.register_parser('argcmd_flags_d', q)\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Flags: slash 1") {
            tester.set_input("argcmd_flags_s /");
            tester.set_expected_matches("/one", "/two", "/twenty");
            tester.run();
        }

        SECTION("Flags: slash 2") {
            tester.set_input("argcmd_flags_s nothing /");
            tester.set_expected_matches("/one", "/two", "/twenty");
            tester.run();
        }

        SECTION("Flags: slash 3") {
            tester.set_input("argcmd_flags_s nothing /tw");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        REQUIRE(lua.do_string("p:disable_file_matching()"));

        SECTION("Flags: slash 4") {
            tester.set_input("argcmd_flags_s /t");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Flags: slash 5") {
            tester.set_input("argcmd_flags_s nothing /");
            tester.set_expected_matches("/one", "/two", "/twenty");
            tester.run();
        }

        SECTION("Flags: slash 6") {
            tester.set_input("argcmd_flags_s nothing /tw");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Flags: slash 7") {
            tester.set_input("argcmd_flags_s out of bounds /t");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Flags: slash 8") {
            tester.set_input("argcmd_flags_s /tw");
            tester.set_expected_matches("/two", "/twenty");
            tester.run();
        }

        SECTION("Flags: dash 1") {
            tester.set_input("argcmd_flags_d -");
            tester.set_expected_matches("-one", "-two", "-twenty");
            tester.run();
        }

        SECTION("Flags: dash 2") {
            tester.set_input("argcmd_flags_d -tw");
            tester.set_expected_matches("-two", "-twenty");
            tester.run();
        }
    }
#endif // MODE4

    SECTION("Skip") {
        const char* script = "\
            q = clink.arg.new_parser()\
            q:set_arguments({ 'two', 'three' })\
            \
            p = clink.arg.new_parser()\
            p:set_arguments({ 'one' }, { 'nine' })\
            p:set_flags('-flag_a' .. q, '-flag_b' .. q)\
            \
            clink.arg.register_parser('argcmd_skip', p)\
        ";

        REQUIRE(lua.do_string(script));

#if MODE4 // flag-type arguments don't work properly yet.
        SECTION("Skip 1") {
            tester.set_input("argcmd_skip one two -fla");
            tester.set_expected_matches("-flag_a", "-flag_b");
            tester.run();
        }
#endif

        SECTION("Skip 2") {
            tester.set_input("argcmd_skip one two -flag_a t");
            tester.set_expected_matches("two", "three");
            tester.run();
        }

#if MODE4 // flag-type arguments don't work properly yet.
        SECTION("Skip 3") {
            tester.set_input("argcmd_skip one two -flag_a two four five -f");
            tester.set_expected_matches("-flag_a", "-flag_b");
            tester.run();
        }
#endif
    }

    SECTION("Lazy init") {
        const char* script = "\
            p = clink.arg.new_parser(\
                '-flag' .. clink.arg.new_parser({ 'red', 'green', 'blue'}),\
                { 'one', 'two', 'three' },\
                { 'four', 'five' }\
            )\
            \
            clink.arg.register_parser('argcmd_lazy', p)\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Lazy init 1") {
            tester.set_input("argcmd_lazy o");
            tester.set_expected_matches("one");
            tester.run();
        }

        SECTION("Lazy init 2") {
            tester.set_input("argcmd_lazy one f");
            tester.set_expected_matches("four", "five");
            tester.run();
        }

        SECTION("Lazy init 2") {
            tester.set_input("argcmd_lazy one four -flag ");
            tester.set_expected_matches("red", "green", "blue");
            tester.run();
        }
    }
}
