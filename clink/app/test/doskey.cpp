// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <lib/doskey.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
static void use_enhanced(bool state)
{
    settings::find("doskey.enhanced")->set(state ? "true" : "false");
}



//------------------------------------------------------------------------------
TEST_CASE("Doskey add/remove")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        REQUIRE(doskey.add_alias("alias", "text") == true);
        REQUIRE(doskey.add_alias("alias", "") == true);
        REQUIRE(doskey.remove_alias("alias") == true);
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey expand : simple")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "text");

        str<> line("alias");

        doskey_alias alias;
        doskey.resolve(line.data(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals("text") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey expand : leading")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "text");

        str<> line(" alias");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(bool(alias) == false);

        REQUIRE(doskey.remove_alias("alias") == true);
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey expand : punctuation")
{
    for (int32 i = 0; i < 2; ++i)
    {
        const char* name = (i == 0) ? "alias" : "\"alias";

        doskey doskey("shell");
        doskey.add_alias(name, "text");

        str<> line("\"alias");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(bool(alias) == (i == 1));

        REQUIRE(doskey.remove_alias(name) == true);
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey args $1-9")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", " $1$2 $3$5$6$7$8$9 "); // no $4 deliberately

        str<> line(L"alias a b c d e f g h i j k l");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(" ab cefghi ") == true);
        REQUIRE(alias.next(line) == false);

        line = "alias a b c d e";

        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(" ab ce ") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey args $*")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", " $* ");

        str<> line(L"alias a b c d e f g h i j k l m n o p");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(" a b c d e f g h i j k l m n o p ") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }

    {
        use_enhanced(false);

        doskey doskey("shell");
        doskey.add_alias("alias", "$*");

        str<> line;
        line << "alias ";
        for (int32 i = 0; i < 12; ++i)
            line << "0123456789abcdef";

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey $? chars")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "$$ $g$G $l$L $b$B $Z");

        str<> line("alias");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals("$ >> << || $Z") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey multi-command")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "one $3 $t $2 two_$T$*three");

        str<> line(L"alias a b c");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals("one c ") == true);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(" b two_") == true);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals("a b cthree") == true);

        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey pipe/redirect")
{
    use_enhanced(false);

    doskey doskey("shell");
    doskey.add_alias("alias", "one $*");

    str<> line("alias|piped");
    doskey_alias alias;
    doskey.resolve(line.c_str(), alias);
    REQUIRE(!alias);

    line = "alias |piped";
    doskey.resolve(line.c_str(), alias);
    REQUIRE(alias);
    REQUIRE(alias.next(line) == true);
    REQUIRE(line.equals("one |piped") == true);

    doskey.remove_alias("alias");
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey pipe/redirect : new")
{
    use_enhanced(true);

    doskey doskey("shell");

    auto test = [&] (const char* input, const char* output) {
        str<> line(input);

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(output) == true, [&] () {
            printf("input:           '%s'\nexpected output: '%s'\nactual output:   '%s'", input, output, line.c_str());
        });
        REQUIRE(alias.next(line) == false);
    };

    doskey.add_alias("alias", "one");
    SECTION("Basic 1")
    { test("alias|piped", "one|piped"); }
    SECTION("Basic 2")
    { test("alias|alias", "one|one"); }
    SECTION("Basic 3")
    { test("alias|alias&alias", "one|one&one"); }
    SECTION("Basic 4")
    { test("&|alias", "&|one"); }
    SECTION("Basic 5")
    { test("alias||", "one||"); }
    SECTION("Basic 6")
    { test("&&alias&|", "&&one&|"); }
    SECTION("Basic 5")
    { test("alias|x|alias", "one|x|one"); }
    doskey.remove_alias("alias");

    #define ARGS "two \"three four\" 5"
    doskey.add_alias("alias", "cmd $1 $2 $3");
    SECTION("Args 1")
    { test("alias " ARGS "|piped", "cmd " ARGS "|piped"); }
    SECTION("Args 2")
    { test("alias " ARGS "|alias " ARGS, "cmd " ARGS "|cmd " ARGS); }
    doskey.remove_alias("alias");
    #undef ARGS

    doskey.add_alias("alias", "powershell \"$*\"");
    SECTION("Args in quotes")
    { test("alias Get-Host|Format-Table", "powershell \"Get-Host|Format-Table\""); }
    doskey.remove_alias("alias");
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey cursor point")
{
    for (int32 i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        str<> line;
        doskey doskey("shell");

        {
            static const int32 c_points[] =
            {
                0,  0,
                2,  2,
                5,  2,
                6,  2,
                7,  3,
                9,  5,
                14, 10,
                20, 16,
                21, 17,
            };

            doskey.add_alias("alias", "qq $*");
            line.clear();
            //                     111111111122
            //           0123456789012345678901
            line.concat("alias  hello world   ");
            //                     11111111
            //           012345678901234567
            //          "qq hello world   "
            for (int32 j = 0; j < sizeof_array(c_points); j += 2)
            {
                const int32 from = c_points[j + 0];
                const int32 expected = c_points[j + 1];
                int32 point = from;
                doskey_alias alias;
                doskey.resolve(line.c_str(), alias, &point);
                REQUIRE(point == expected, [&] () {
                    printf("%sFROM %d:\nexpected: %d\nactual:   %d", i ? "(enhanced)\n" : "", from, expected, point);
                });
            }
            doskey.remove_alias("alias");
        }

        {
            static const int32 c_points[] =
            {
                0,  0,
                3,  5,
                4,  11,
                5,  12,
                6,  13,
                7,  18,
                9,  18,
                12, 18,
                13, 18,
                14, 6,
                20, 18,
                23, 18,
            };

            doskey.add_alias("az", "world $3 $1 xyz");
            line.clear();
            //                     11111111112222
            //           012345678901234567890123
            line.concat("az  abc hello blah     ");
            //                     111111111
            //           0123456789012345678
            //          "world blah abc xyz"
            for (int32 j = 0; j < sizeof_array(c_points); j += 2)
            {
                const int32 from = c_points[j + 0];
                const int32 expected = c_points[j + 1];
                int32 point = from;
                doskey_alias alias;
                doskey.resolve(line.c_str(), alias, &point);
                REQUIRE(point == expected, [&] () {
                    printf("%sFROM %d:\nexpected: %d\nactual:   %d", i ? "(enhanced)\n" : "", from, expected, point);
                });
            }
            doskey.remove_alias("az");
        }
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey cursor point : multiple commands")
{
    use_enhanced(true);

    str<> line;
    doskey doskey("shell");

    static const char prolog[] = "abc & ";
    static const char epilog[] = " & xyz";
    const int32 prolog_len = int32(strlen(prolog));
    const int32 epilog_len = int32(strlen(epilog));

    {
        static const int32 c_points[] =
        {
            0,  0,
            2,  2,
            5,  2,
            6,  2,
            7,  3,
            9,  5,
            14, 10,
            20, 16,
            21, 17,
        };

        doskey.add_alias("alias", "qq $*");
        //                       111111111122
        //             0123456789012345678901
        line.format("%salias  hello world   %s", prolog, epilog);
        //                       11111111
        //             012345678901234567
        //            "qq hello world   "
        for (int32 j = 0; j < sizeof_array(c_points); j += 2)
        {
            const int32 from = prolog_len + c_points[j + 0];
            const int32 expected = prolog_len + c_points[j + 1];
            int32 point = from;
            doskey_alias alias;
            doskey.resolve(line.c_str(), alias, &point);
            REQUIRE(point == expected, [&] () {
                printf("FROM %d:\nexpected: %d\nactual:   %d", from, expected, point);
            });
        }

        for (int32 j = 0; j < prolog_len; j++)
        {
            const int32 from = j;
            const int32 expected = j;
            int32 point = from;
            doskey_alias alias;
            doskey.resolve(line.c_str(), alias, &point);
            REQUIRE(point == expected, [&] () {
                printf("(prolog)\nFROM %d:\nexpected: %d\nactual:   %d", from, expected, point);
            });
        }
        for (int32 j = 0; j < epilog_len; j++)
        {
            const int32 from = line.length() - j;
            int32 point = from;
            doskey_alias alias;
            doskey.resolve(line.c_str(), alias, &point);
            const int32 expected = alias.UNITTEST_get_stream().length() - j;
            REQUIRE(point == expected, [&] () {
                printf("(epilog)\nFROM %d:\nexpected: %d\nactual:   %d", from, expected, point);
            });
        }

        doskey.remove_alias("alias");
    }

    {
        static const int32 c_points[] =
        {
            0,  0,
            3,  5,
            4,  11,
            5,  12,
            6,  13,
            7,  18,
            9,  18,
            12, 18,
            13, 18,
            14, 6,
            20, 18,
            23, 18,
        };

        doskey.add_alias("az", "world $3 $1 xyz");
        //                       11111111112222
        //             012345678901234567890123
        line.format("%saz  abc hello blah     %s", prolog, epilog);
        //                       111111111
        //             0123456789012345678
        //            "world blah abc xyz"
        for (int32 j = 0; j < sizeof_array(c_points); j += 2)
        {
            const int32 from = prolog_len + c_points[j + 0];
            const int32 expected = prolog_len + c_points[j + 1];
            int32 point = from;
            doskey_alias alias;
            doskey.resolve(line.c_str(), alias, &point);
            REQUIRE(point == expected, [&] () {
                printf("FROM %d:\nexpected: %d\nactual:   %d", from, expected, point);
            });
        }

        for (int32 j = 0; j < prolog_len; j++)
        {
            const int32 from = j;
            const int32 expected = j;
            int32 point = from;
            doskey_alias alias;
            doskey.resolve(line.c_str(), alias, &point);
            REQUIRE(point == expected, [&] () {
                printf("(prolog)\nFROM %d:\nexpected: %d\nactual:   %d", from, expected, point);
            });
        }
        for (int32 j = 0; j < epilog_len; j++)
        {
            const int32 from = line.length() - j;
            int32 point = from;
            doskey_alias alias;
            doskey.resolve(line.c_str(), alias, &point);
            const int32 expected = alias.UNITTEST_get_stream().length() - j;
            REQUIRE(point == expected, [&] () {
                printf("(epilog)\nFROM %d:\nexpected: %d\nactual:   %d", from, expected, point);
            });
        }

        doskey.remove_alias("az");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey cursor point : grouping parens")
{
    str<> line;
    doskey doskey("shell");

    for (int32 i = 0; i < 2; ++i)
    {
        const bool enhanced = (i != 0);
        use_enhanced(enhanced);

        static const int32 c_points[] =
        {
            0,  0,
            2,  2,
            6,  4,
            7,  4,
            8,  5,
            10, 7,
            12, 9,
            14, 11,
            16, 13,
            19, 16,
            20, 17,
            24, 19,
            25, 19,
            26, 20,
            28, 22,
            30, 24,
            31, 25,
            32, 26,
            33, 27,
        };

        doskey.add_alias("alias", "qq $*");
        //                111111111122222222223333
        //      0123456789012345678901234567890123
        line = "( alias hello ) & ( alias world )";
        //                111111111122222222
        //      0123456789012345678901234567
        //     "( qq hello ) & ( qq world )"
        for (int32 j = 0; j < sizeof_array(c_points); j += 2)
        {
            const int32 from = c_points[j + 0];
            const int32 expected = (enhanced || from <= 20) ? c_points[j + 1] : from - 3;
            int32 point = from;
            doskey_alias alias;
            doskey.resolve(line.c_str(), alias, &point);
            REQUIRE(point == expected, [&] () {
                printf("FROM %d:\nexpected: %d\nactual:   %d", from, expected, point);
            });
        }

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey issue 773")
{
    static const char* dir_fs[] = {
        "foo.txt",
        "ver.txt",
        nullptr,
    };

    fs_fixture fs(dir_fs);

    doskey doskey("clink_test_harness");
    doskey.add_alias("dir", "program $*");

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua_load_script(lua, app, exec);

    line_editor::desc desc(nullptr, nullptr, nullptr, nullptr);
    line_editor_tester tester(desc, nullptr, nullptr);
    tester.get_editor()->set_generator(lua_generator);
    tester.set_tab_binding("old-menu-complete");

    str<> cmd;
    str<> result;
    static const char* const c_pgm[] = { "bar", "dir" };
    auto make = [&](int32 i, const char* line, str<>& out)
    {
        out.format(line, c_pgm[i]);
    };
    auto runtest = [&]()
    {
        tester.set_input(cmd.c_str());
        tester.set_expected_output(result.c_str());
        tester.run();
    };

    for (int32 i = 0; i < 2; ++i)
    {
        make(i, "%s asdf\t", cmd);
        make(i, "%s asdf", result);
        runtest();

        make(i, "%s asdf.\t", cmd);
        make(i, "%s asdf.", result);
        runtest();

        make(i, "%s foo\t", cmd);
        make(i, "%s foo.txt ", result);
        runtest();

        make(i, "%s foo.\t", cmd);
        make(i, "%s foo.txt ", result);
        runtest();

        make(i, "%s foo.t\t", cmd);
        make(i, "%s foo.txt ", result);
        runtest();

        make(i, "%s ver\t", cmd);
        make(i, "%s ver.txt ", result);
        runtest();

        make(i, "%s ver.\t", cmd);
        make(i, "%s ver.txt ", result);
        runtest();

        make(i, "%s ver.t\t", cmd);
        make(i, "%s ver.txt ", result);
        runtest();
    }

    doskey.remove_alias("dir");
}
