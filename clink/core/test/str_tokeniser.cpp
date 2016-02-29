// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <core/str_tokeniser.h>

TEST_CASE("str_tokeniser : basic")
{
    str<> s;
    str_tokeniser t("a;b;c", ";");
    REQUIRE(t.next(s)); REQUIRE(s.equals("a") == true);
    REQUIRE(t.next(s)); REQUIRE(s.equals("b") == true);
    REQUIRE(t.next(s)); REQUIRE(s.equals("c") == true);
    REQUIRE(t.next(s) == false);
}

TEST_CASE("str_tokeniser : multi delims")
{
    str<> s;
    str_tokeniser t("a;b-c.d", ";-.");
    REQUIRE(t.next(s)); REQUIRE(s.equals("a") == true);
    REQUIRE(t.next(s)); REQUIRE(s.equals("b") == true);
    REQUIRE(t.next(s)); REQUIRE(s.equals("c") == true);
    REQUIRE(t.next(s)); REQUIRE(s.equals("d") == true);
    REQUIRE(t.next(s) == false);
}

TEST_CASE("str_tokeniser : ends")
{
    str<> s;
    auto inputs = { "a;b;c", ";a;b;c", "a;b;c;" };
    for (auto input : inputs)
    {
        str_tokeniser t(input, ";");
        REQUIRE(t.next(s)); REQUIRE(s.equals("a") == true);
        REQUIRE(t.next(s)); REQUIRE(s.equals("b") == true);
        REQUIRE(t.next(s)); REQUIRE(s.equals("c") == true);
        REQUIRE(t.next(s) == false);
    }
}

TEST_CASE("str_tokeniser : delim runs")
{
    str<> s;
    auto inputs = { "a;;b--c", "-;a;-b;c", "a;b;-c-;" };
    for (auto input : inputs)
    {
        str_tokeniser t(input, ";-");
        REQUIRE(t.next(s)); REQUIRE(s.equals("a") == true);
        REQUIRE(t.next(s)); REQUIRE(s.equals("b") == true);
        REQUIRE(t.next(s)); REQUIRE(s.equals("c") == true);
        REQUIRE(t.next(s) == false);
    }
}
