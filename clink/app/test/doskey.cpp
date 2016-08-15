// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <host/doskey.h>

//------------------------------------------------------------------------------
TEST_CASE("Doskey add/remove") {
    doskey doskey("shell");
    REQUIRE(doskey.add_alias("alias", "text") == true);
    REQUIRE(doskey.add_alias("alias", "") == true);
    REQUIRE(doskey.remove_alias("alias") == true);
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey expand") {
    doskey doskey("shell");
    doskey.add_alias("alias", "text");

    wstr<> line(L"alias");
    REQUIRE(doskey.begin(line.data(), line.size()) == true);
    REQUIRE(line.equals(L"text") == true);
    REQUIRE(doskey.next(line.data(), line.size()) == false);

    doskey.remove_alias("alias");
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey args $1-9") {
    doskey doskey("shell");
    doskey.add_alias("alias", " $1$2 $3$5$6$7$8$9 "); // no $4 deliberately

    wstr<> line(L"alias a b c d e f g h i j k l");
    REQUIRE(doskey.begin(line.data(), line.size()) == true);
    REQUIRE(line.equals(L" ab cefghi ") == true);
    REQUIRE(doskey.next(line.data(), line.size()) == false);

    line = L"alias a b c d e";
    REQUIRE(doskey.begin(line.data(), line.size()) == true);
    REQUIRE(line.equals(L" ab ce ") == true);
    REQUIRE(doskey.next(line.data(), line.size()) == false);

    doskey.remove_alias("alias");
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey args $*") {
    doskey doskey("shell");
    doskey.add_alias("alias", " $* ");

    wstr<> line(L"alias a b c d e f g h i j k l m n o p");
    REQUIRE(doskey.begin(line.data(), line.size()) == true);
    REQUIRE(line.equals(L" a b c d e f g h i j k l m n o p ") == true);
    REQUIRE(doskey.next(line.data(), line.size()) == false);

    doskey.remove_alias("alias");
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey $? chars") {
    doskey doskey("shell");
    doskey.add_alias("alias", "$$ $g$G $l$L $b$B $Z");

    wstr<> line(L"alias");
    REQUIRE(doskey.begin(line.data(), line.size()) == true);
    REQUIRE(line.equals(L"$ >> << || $Z") == true);
    REQUIRE(doskey.next(line.data(), line.size()) == false);

    doskey.remove_alias("alias");
}

//------------------------------------------------------------------------------
TEST_CASE("Doskey multi-command") {
    doskey doskey("shell");
    doskey.add_alias("alias", "one $3 $t $2 two$T$*three");

    wstr<> line(L"alias a b c");
    REQUIRE(doskey.begin(line.data(), line.size()) == true);
    REQUIRE(line.equals(L"one c ") == true);

    REQUIRE(doskey.next(line.data(), line.size()) == true);
    REQUIRE(line.equals(L" b two") == true);

    REQUIRE(doskey.next(line.data(), line.size()) == true);
    REQUIRE(line.equals(L"a b cthree") == true);

    REQUIRE(doskey.next(line.data(), line.size()) == false);

    doskey.remove_alias("alias");
}
