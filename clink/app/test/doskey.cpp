// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/settings.h>
#include <core/str.h>
#include <host/doskey.h>

//------------------------------------------------------------------------------
static void use_enhanced(bool state)
{
    settings::find("doskey.enhanced")->set(state ? "true" : "false");
}



//------------------------------------------------------------------------------
TEST_CASE("Doskey add/remove")
{
    for (int i = 0; i < 2; ++i)
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
    for (int i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "text");

        wstr<> line(L"alias");

        doskey_alias alias;
        doskey.resolve(line.data(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L"text") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey expand : leading")
{
    for (int i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "text");

        wstr<> line(L" alias");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(bool(alias) == (i == 1));

        doskey.add_alias("\"alias", "text\"");

        REQUIRE(doskey.remove_alias("alias") == true);
        REQUIRE(doskey.remove_alias("\"alias") == true);
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey args $1-9")
{
    for (int i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", " $1$2 $3$5$6$7$8$9 "); // no $4 deliberately

        wstr<> line(L"alias a b c d e f g h i j k l");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L" ab cefghi ") == true);
        REQUIRE(alias.next(line) == false);

        line = L"alias a b c d e";

        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L" ab ce ") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey args $*")
{
    for (int i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", " $* ");

        wstr<> line(L"alias a b c d e f g h i j k l m n o p");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L" a b c d e f g h i j k l m n o p ") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey $? chars")
{
    for (int i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "$$ $g$G $l$L $b$B $Z");

        wstr<> line(L"alias");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L"$ >> << || $Z") == true);
        REQUIRE(alias.next(line) == false);

        doskey.remove_alias("alias");
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey multi-command")
{
    for (int i = 0; i < 2; ++i)
    {
        use_enhanced(i != 0);

        doskey doskey("shell");
        doskey.add_alias("alias", "one $3 $t $2 two_$T$*three");

        wstr<> line(L"alias a b c");

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L"one c ") == true);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L" b two_") == true);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(L"a b cthree") == true);

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

    wstr<> line(L"alias|piped");
    doskey_alias alias;
    doskey.resolve(line.c_str(), alias);
    REQUIRE(!alias);

    line = L"alias |piped";
    doskey.resolve(line.c_str(), alias);
    REQUIRE(alias);
    REQUIRE(alias.next(line) == true);
    REQUIRE(line.equals(L"one |piped") == true);

    doskey.remove_alias("alias");
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey pipe/redirect : new")
{
    use_enhanced(true);

    doskey doskey("shell");

    auto test = [&] (const wchar_t* input, const wchar_t* output) {
        wstr<> line(input);

        doskey_alias alias;
        doskey.resolve(line.c_str(), alias);
        REQUIRE(alias);

        REQUIRE(alias.next(line) == true);
        REQUIRE(line.equals(output) == true);
        REQUIRE(alias.next(line) == false);
    };

    doskey.add_alias("alias", "one");
    SECTION("Basic 1")
    { test(L"alias|piped", L"one|piped"); }
    SECTION("Basic 2")
    { test(L"alias|alias", L"one|one"); }
    SECTION("Basic 3")
    { test(L"alias|alias&alias", L"one|one&one"); }
    SECTION("Basic 4")
    { test(L"&|alias", L"&|one"); }
    SECTION("Basic 5")
    { test(L"alias||", L"one||"); }
    SECTION("Basic 6")
    { test(L"&&alias&|", L"&&one&|"); }
    SECTION("Basic 5")
    { test(L"alias|x|alias", L"one|x|one"); }
    doskey.remove_alias("alias");

    #define ARGS L"two \"three four\" 5"
    doskey.add_alias("alias", "cmd $1 $2 $3");
    SECTION("Args 1")
    { test(L"alias " ARGS L"|piped", L"cmd " ARGS L"|piped"); }
    SECTION("Args 2")
    { test(L"alias " ARGS L"|alias " ARGS, L"cmd " ARGS L"|cmd " ARGS); }
    doskey.remove_alias("alias");
    #undef ARGS
}
