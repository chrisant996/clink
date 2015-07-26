// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "catch.hpp"

#include <core/str.h>
#include <core/str_compare.h>

TEST_CASE("String compare") {
    SECTION("Exact") {
        str_compare_scope _(str_compare_scope::exact);

        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == 0);

        REQUIRE(str_compare("abc", "ab") == 2);
        REQUIRE(str_compare("ab", "abc") == 2);

        REQUIRE(str_compare("_", "_") == -1);
        REQUIRE(str_compare("-", "_") == 0);
    }

    SECTION("Case insensitive") {
        str_compare_scope _(str_compare_scope::caseless);

        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == -1);

        REQUIRE(str_compare("aBc", "ab") == 2);
        REQUIRE(str_compare("ab", "aBc") == 2);

        REQUIRE(str_compare("_", "_") == -1);
        REQUIRE(str_compare("-", "_") == 0);
    }

    SECTION("Relaxed") {
        str_compare_scope _(str_compare_scope::relaxed);

        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == -1);

        REQUIRE(str_compare("abc", "ab") == 2);
        REQUIRE(str_compare("ab", "abc") == 2);

        REQUIRE(str_compare("_", "_") == -1);
        REQUIRE(str_compare("-", "_") == -1);
    }

    SECTION("UTF-8/UTF-16 mix") {
        str_compare_scope _(str_compare_scope::exact);

        REQUIRE(str_compare(L"abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("abc123!@#", L"abc123!@#") == -1);
    }

    SECTION("Scopes") {
        str_compare_scope outer(str_compare_scope::exact);
        {
            str_compare_scope inner(str_compare_scope::caseless);
            {
                str_compare_scope inner(str_compare_scope::relaxed);
                REQUIRE(str_compare("-", "_") == -1);
            }
            REQUIRE(str_compare("ABC123!@#", "abc123!@#") == -1);
        }
        REQUIRE(str_compare("abc123!@#", "abc123!@#") == -1);
        REQUIRE(str_compare("ABC123!@#", "abc123!@#") == 0);
        REQUIRE(str_compare("-", "_") == 0);
    }
}
