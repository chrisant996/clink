// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/path.h>
#include <core/settings.h>
#include <core/os.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_word_classifier.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
};

//------------------------------------------------------------------------------
TEST_CASE("Lua word classification")
{
    wchar_t* host = const_cast<wchar_t*>(os::get_shellname());

    const char* empty_fs[] = { nullptr };
    fs_fixture fs(empty_fs);

    lua_state lua;
    lua_match_generator lua_generator(lua); // This loads the required lua scripts.
    lua_load_script(lua, app, cmd);
    lua_load_script(lua, app, dir);
    lua_word_classifier lua_classifier(lua);

    settings::find("clink.colorize_input")->set("true");

    line_editor::desc desc(nullptr, nullptr, nullptr, nullptr);
    line_editor_tester tester(desc, "&|", nullptr);
    tester.get_editor()->set_generator(lua_generator);
    tester.get_editor()->set_classifier(lua_classifier);

    AddConsoleAliasW(const_cast<wchar_t*>(L"dkalias"), const_cast<wchar_t*>(L"text"), host);

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

        REQUIRE_LUA_DO_STRING(lua, script);

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

        SECTION("Separator && 2")
        {
            tester.set_input("nullcmd &&cd t");
            tester.set_expected_classifications("oco");
            tester.run();
        }

        SECTION("Separator && 3")
        {
            tester.set_input("nullcmd \"&&\" && cd t");
            tester.set_expected_classifications("o co");
            tester.run();
        }

        SECTION("Separator && 4")
        {
            tester.set_input("nullcmd \"&&\"&&cd t");
            tester.set_expected_classifications("o co");
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
            tester.set_expected_classifications("oano c ofaoo");
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

    SECTION("Linked")
    {
        const char* script = "\
            local test_parser = clink.argmatcher()\
            test_parser:addflags('-h', '--help', '-show_output', '-flag1', '-optionB')\
            test_parser:addarg({'arg1', 'arg2'})\
            test_parser:addarg({'something', 'else'})\
            test_parser:nofiles()\
            \
            local popping_parser = clink.argmatcher()\
            popping_parser:addflags('-z', '--zoom')\
            popping_parser:addarg({'ABC', 'DEF'})\
            popping_parser:addarg({'ETWAS', 'ANDERS'})\
            \
            local foo_parser = clink.argmatcher('foo')\
            foo_parser:addflags('-a', '-b', '-c')\
            foo_parser:addarg({'test' .. test_parser, 'popping' .. popping_parser})\
            foo_parser:addarg({'qrs', 'tuv', 'wxyz'})\
            \
            local oldtest_parser = clink.arg.new_parser()\
            oldtest_parser:add_flags('-h', '--help', '-show_output', '-flag1', '-optionB')\
            oldtest_parser:add_arguments({'arg1', 'arg2'})\
            oldtest_parser:add_arguments({'something', 'else'})\
            \
            local oldfoo_parser = clink.arg.new_parser()\
            oldfoo_parser:add_flags('-a', '-b', '-c')\
            oldfoo_parser:add_arguments({'test' .. oldtest_parser})\
            oldfoo_parser:add_arguments({'qrs', 'tuv', 'wxyz'})\
            \
            clink.arg.register_parser('oldfoo', oldfoo_parser)\
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        SECTION("Flag at end")
        {
            // Make sure it doesn't pop, so the last word is recognized as a
            // flag by the correct argmatcher.

            tester.set_input("foo popping ABC ETWAS -z");
            tester.set_expected_classifications("oaaaf");
            tester.run();

            tester.set_input("foo test arg1 something -h");
            tester.set_expected_classifications("oaaaf");
            tester.run();

            tester.set_input("oldfoo test arg1 something -h");
            tester.set_expected_classifications("oaaaf");
            tester.run();
        }

        SECTION("Arg at end")
        {
            // Make sure it can pop, so the last word is recognized as an arg by
            // the correct argmatcher.
            tester.set_input("foo popping ABC ETWAS qrs");
            tester.set_expected_classifications("oaaaa");
            tester.run();

            // Make sure nofiles doesn't pop.
            tester.set_input("foo test arg1 something qrs");
            tester.set_expected_classifications("oaaan");
            tester.run();

            // Make sure backward compatibility mode doesn't pop, even if
            // :disable_file_matching() is not used.
            tester.set_input("oldfoo test arg1 something qrs");
            tester.set_expected_classifications("oaaao");
            tester.run();
        }
    }

    SECTION("Paired")
    {
        const char* script = "\
            local q = clink.argmatcher():addarg('x', 'y', 'z')\
            local p = clink.argmatcher():addarg('red', 'green', 'blue', 'mode='..q)\
            clink.argmatcher('paired')\
            :addflags({'--abc:'..p, '--def='..p, '--xyz'..p})\
            :addarg('AA', 'BB')\
            :addarg('XX', 'YY')\
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        SECTION("Equal word")
        {
            tester.set_input("paired --xyz mode=x");
            tester.set_expected_classifications("ofaa");
        }

        SECTION("Equal text")
        {
            tester.set_input("paired --xyz mode=qqq");
            tester.set_expected_classifications("ofao");
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

    SECTION("nowordbreakchars")
    {
        static const char* bat_fs[] = {
            "bat.bat",
            nullptr,
        };

        fs_fixture fs_bat(bat_fs);

        const char* script = "\
            clink.argmatcher('qq'):addflags({nowordbreakchars=',','-,','-a,b'}):addarg({nowordbreakchars=',','abc','xyz','mm,ab','mm,ac','mm,xy'})\
            clink.argmatcher('exe'):addflags({'-,','-a,b'}):addarg({'abc','xyz','mm,ab','mm,ac','mm,xy'})\
            clink.argmatcher('bat'):addflags({'-,','-a,b'}):addarg({'abc','xyz','mm,ab','mm,ac','mm,xy'})\
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        SECTION("Basic")
        {
            tester.set_input("qq -,");
            tester.set_expected_faces("oo ff");
            tester.run();

            tester.set_input("qq -,abc");
            tester.set_expected_faces("oo ooooo");
            tester.run();

            tester.set_input("qq -, abc");
            tester.set_expected_faces("oo ff aaa");
            tester.run();

            tester.set_input("qq -a,b");
            tester.set_expected_faces("oo ffff");
            tester.run();

            tester.set_input("qq mm,ab");
            tester.set_expected_faces("oo aaaaa");
            tester.run();

            tester.set_input("qq mm,a");
            tester.set_expected_faces("oo oooo");
            tester.run();

            tester.set_input("qq mm,abc");
            tester.set_expected_faces("oo oooooo");
            tester.run();
        }

        SECTION("auto exe")
        {
            tester.set_input("exe -,");
            tester.set_expected_faces("ooo ff");
            tester.run();
        }

        SECTION("auto bat")
        {
            tester.set_input("bat -,");
            tester.set_expected_faces("ooo o ");
            tester.run();
        }
    }

    AddConsoleAliasW(const_cast<wchar_t*>(L"dkalias"), nullptr, host);
}
