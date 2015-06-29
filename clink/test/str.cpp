/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "catch.hpp"
#include <core/str.h>

#ifndef STR
#define STR(x) x
#define NAME_SUFFIX " (char)"
#endif

TEST_CASE("Strings" NAME_SUFFIX) {
    SECTION("Basics") {
        str<256> s;
        REQUIRE(s.length() == 0);
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
    }

    SECTION("Concatenation") {
        str<4> s;
        int ones = ~0;

        REQUIRE(s.copy(STR("123")) == true);
        REQUIRE(s.copy(STR("1234")) == false);
        REQUIRE(ones == ~0);

        s.clear();
        REQUIRE(s.copy(STR("123456789abcdef")) == false);
        REQUIRE(ones == ~0);

        s.clear();
        REQUIRE(s.concat(STR("1234"), 3) == true);
        REQUIRE(s.length() == 3);

        s.clear();
        REQUIRE(s.concat(STR("1234"), 4) == false);

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

    SECTION("Truncate") {
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

    SECTION("Index of") {
        str<16> s;
        s << STR("AaBbbBaA");

        REQUIRE(s.first_of('A') == 0);
        REQUIRE(s.first_of('Z') == -1);
        REQUIRE(s.last_of('A') == 7);
        REQUIRE(s.last_of('Z') == -1);
    }

    SECTION("Equality") {
        str<16> s;
        s.copy(STR("aBc"));

        REQUIRE(s.equals(STR("aBc")) == true);
        REQUIRE(s.equals(STR("abc")) == false);
        REQUIRE(s.iequals(STR("abc")) == true);
    }

    SECTION("Format") {
        str<> s;

        REQUIRE(s.format(STR("%d"), 123) == true);
        REQUIRE(s.equals(STR("123")));
    }

    SECTION("Operators") {
        str<> s;

        s << STR("abc");
        REQUIRE(s.equals(STR("abc")) == true);

        s << STR("123");
        REQUIRE(s.equals(STR("abc123")) == true);

        s.clear();
        s << STR("abcd") << STR("1234");
        REQUIRE(s.equals(STR("abcd1234")) == true);
    }
}

#undef STR
#undef NAME_SUFFIX
