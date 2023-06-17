// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>

#ifndef STR
#define STR(x) x
#define NAME_SUFFIX " (char)"
#endif

//------------------------------------------------------------------------------
TEST_CASE("Strings" NAME_SUFFIX)
{
    SECTION("Basics")
    {
        str<256> s;
        REQUIRE(s.length() == 0);
        REQUIRE(s.char_count() == s.length());
        REQUIRE(s.data() == s.c_str());
        REQUIRE(s.size() == 256);

        s.copy(STR("123"));
        REQUIRE(s.length() == 3);

        s.clear();
        REQUIRE(s.length() == 0);

        s.copy(STR("0123456789"));
        REQUIRE(s[0] == '0');
        REQUIRE(s[9] == '9');
        REQUIRE(s[99] == 0);
        REQUIRE(s.length() == s.char_count());

        s.clear();
        s << nullptr;
        REQUIRE(s.length() == 0);
    }

    SECTION("Concatenation (growable)")
    {
        str<4> s;
        int32 ones = ~0;

        REQUIRE(s.copy(STR("123")) == true);
        REQUIRE(s.equals(STR("123")) == true);

        REQUIRE(s.copy(STR("1234")) == true);
        REQUIRE(s.equals(STR("1234")) == true);
        REQUIRE(ones == ~0);

        s.clear();
        REQUIRE(s.copy(STR("123456789abcdef")) == true);
        REQUIRE(ones == ~0);

        s.clear();
        REQUIRE(s.concat(STR("1234"), 3) == true);
        REQUIRE(s.length() == 3);

        s.clear();
        REQUIRE(s.concat(STR("1234"), 4) == true);
        REQUIRE(s.length() == 4);

        s.clear();
        REQUIRE(s.concat(STR("1234"), 0) == true);
        REQUIRE(s.concat(STR("1234"), 1) == true);
        REQUIRE(s.equals(STR("1")) == true);

        REQUIRE(s.concat(STR("2"), 1) == true);
        REQUIRE(s.length() == 2);

        REQUIRE(s.concat(STR("345678"), 2) == true);
        REQUIRE(s.equals(STR("1234")) == true);
        REQUIRE(ones == ~0);
    }

    SECTION("Concatenation (fixed)")
    {
        str<4, false> s;
        int32 ones = ~0;

        REQUIRE(s.copy(STR("123")) == true);
        REQUIRE(s.equals(STR("123")) == true);

        REQUIRE(s.copy(STR("1234")) == false);
        REQUIRE(s.equals(STR("123")) == true);
        REQUIRE(ones == ~0);

        s.clear();
        REQUIRE(s.copy(STR("123456789abc")) == false);
        REQUIRE(s.equals(STR("123")) == true);
        REQUIRE(s.size() == 4);
        REQUIRE(ones == ~0);

        s.clear();
        REQUIRE(s.concat(STR("1234"), 0) == true);
        REQUIRE(s.concat(STR("1234"), 1) == true);
        REQUIRE(s.equals(STR("1")) == true);

        REQUIRE(s.concat(STR("2"), 1) == true);
        REQUIRE(s.length() == 2);

        REQUIRE(s.concat(STR("345678"), 2) == false);
        REQUIRE(s.equals(STR("123")) == true);
        REQUIRE(ones == ~0);
    }

    SECTION("Truncate")
    {
        str<16> s;
        s << STR("01234567");

        REQUIRE(s.length() == 8);

        s.truncate(0x7fffffff);
        REQUIRE(s.length() == 8);

        s.truncate(0x80000000);
        REQUIRE(s.length() == 8);

        s.truncate(4);
        REQUIRE(s.length() == 4);
        REQUIRE(s.equals(STR("0123")) == true);
    }

    SECTION("Index of")
    {
        str<16> s;
        s << STR("AaBbbBaA");

        REQUIRE(s.first_of('A') == 0);
        REQUIRE(s.first_of('Z') == -1);
        REQUIRE(s.last_of('A') == 7);
        REQUIRE(s.last_of('Z') == -1);
    }

    SECTION("Equality")
    {
        str<16> s;
        s.copy(STR("aBc"));

        REQUIRE(s.equals(STR("aBc")) == true);
        REQUIRE(s.equals(STR("abc")) == false);
        REQUIRE(s.iequals(STR("abc")) == true);
    }

    SECTION("Format growable")
    {
        str<6> s;

        REQUIRE(s.format(STR("%d"), 123) == true);
        REQUIRE(s.equals(STR("123")));

        REQUIRE(s.format(STR("%d"), 1234567) == true);
        REQUIRE(s.equals(STR("1234567")));
    }

    SECTION("Format fixed")
    {
        str<6, false> s;

        REQUIRE(s.format(STR("%d"), 123) == true);
        REQUIRE(s.equals(STR("123")));

        REQUIRE(s.format(STR("%d"), 1234567) == false);
        REQUIRE(s.equals(STR("12345")));
    }

    SECTION("Operators")
    {
        str<> s;

        s << STR("abc");
        REQUIRE(s.equals(STR("abc")) == true);

        s << STR("123");
        REQUIRE(s.equals(STR("abc123")) == true);

        s.clear();
        s << STR("abcd") << STR("1234");
        REQUIRE(s.equals(STR("abcd1234")) == true);
    }

    SECTION("Construction")
    {
        char buffer[] = "test";
        REQUIRE(str_base(buffer).equals("test") == true);
    }
}

#undef STR
#undef NAME_SUFFIX
