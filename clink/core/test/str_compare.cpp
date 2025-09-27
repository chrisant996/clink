// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include <core/str.h>
#include <core/str_compare.h>

//------------------------------------------------------------------------------
TEST_CASE("String compare")
{
    SECTION("Exact")
    {
        str_compare_scope _(str_compare_scope::exact, false);

        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == 0);

        REQUIRE(str_compare("abc", "ab") == 2);
        REQUIRE(str_compare("ab", "abc") == 2);

        REQUIRE(str_compare("_", "_") == -1);
        REQUIRE(str_compare("-", "_") == 0);
    }

    SECTION("Case insensitive")
    {
        str_compare_scope _(str_compare_scope::caseless, false);

        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == -1);

        REQUIRE(str_compare("aBc", "ab") == 2);
        REQUIRE(str_compare("ab", "aBc") == 2);

        REQUIRE(str_compare("_", "_") == -1);
        REQUIRE(str_compare("-", "_") == 0);
    }

    SECTION("Relaxed")
    {
        str_compare_scope _(str_compare_scope::relaxed, false);

        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == -1);

        REQUIRE(str_compare("abc", "ab") == 2);
        REQUIRE(str_compare("ab", "abc") == 2);

        REQUIRE(str_compare("_", "_") == -1);
        REQUIRE(str_compare("-", "_") == -1);
    }

    SECTION("Scopes")
    {
        str_compare_scope outer(str_compare_scope::exact, false);
        {
            str_compare_scope inner(str_compare_scope::caseless, false);
            {
                str_compare_scope inner(str_compare_scope::relaxed, false);
                REQUIRE(str_compare("-", "_") == -1);
            }
            REQUIRE(str_compare("ABC123!@#", "abc123!@#") == -1);
        }
        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == 0);
        REQUIRE(str_compare("-", "_") == 0);
    }

    SECTION("Iterator state (same)")
    {
        str_iter lhs_iter("abc123");
        str_iter rhs_iter("abc123");

        REQUIRE(str_compare(lhs_iter, rhs_iter) == -1);
        REQUIRE(lhs_iter.more() == false);
        REQUIRE(rhs_iter.more() == false);
    }

    SECTION("Iterator state (different)")
    {
        str_iter lhs_iter("abc123");
        str_iter rhs_iter("abc321");

        REQUIRE(str_compare(lhs_iter, rhs_iter) == 3);
        REQUIRE(lhs_iter.peek() == '1');
        REQUIRE(rhs_iter.peek() == '3');
    }

    SECTION("Iterator state (lhs shorter)")
    {
        str_iter lhs_iter("abc");
        str_iter rhs_iter("abc321");

        REQUIRE(str_compare(lhs_iter, rhs_iter) == 3);
        REQUIRE(lhs_iter.more() == false);
        REQUIRE(rhs_iter.peek() == '3');
    }

    SECTION("Iterator state (lhs shorter)")
    {
        str_iter lhs_iter("abc123");
        str_iter rhs_iter("abc");

        REQUIRE(str_compare(lhs_iter, rhs_iter) == 3);
        REQUIRE(lhs_iter.peek() == '1');
        REQUIRE(rhs_iter.more() == false);
    }

    SECTION("UTF-8")
    {
        REQUIRE(str_compare("\xc2\x80", "\xc2\x80") == -1);
        REQUIRE(str_compare("\xc2\x80""abc", "\xc2\x80") == 2);
    }

    SECTION("UTF-16")
    {
        REQUIRE(str_compare(L"abc123", L"abc123") == -1);
        REQUIRE(str_compare(L"\xd800\xdc00" L"abc", L"\xd800\xdc00") == 2);
    }
}
