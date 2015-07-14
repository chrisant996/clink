// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "catch.hpp"
#include <core/str.h>

TEST_CASE("Wide character/UTF-8 conversion") {
    SECTION("To UTF-8") {
        str<> s;

        SECTION("One byte") {
            s.convert(L"0123456789");
            REQUIRE(s.equals("0123456789"));

            s.convert(L"\x01\x7f");
            REQUIRE(s.equals("\x01\x7f"));

            s.convert(L"AaBbCc");
            REQUIRE(s.equals("AaBbCc"));
        }

        SECTION("Two bytes") {
            s.convert(L"\x0080");
            REQUIRE(s.equals("\xc2\x80"));

            s.convert(L"\x07ff");
            REQUIRE(s.equals("\xdf\xbf"));
        }

        SECTION("Three bytes") {
            s.convert(L"\x0800");
            REQUIRE(s.equals("\xe0\xa0\x80"));

            s.convert(L"\xffff");
            REQUIRE(s.equals("\xef\xbf\xbf"));
        }

        SECTION("Four bytes") {
            s.convert(L"\xd800\xdc00");
            REQUIRE(s.equals("\xf0\x90\x80\x80"));

            s.convert(L"\xdbff\xdfff");
            REQUIRE(s.equals("\xf4\x8f\xbf\xbf"));
        }

        SECTION("char_count()") {
            REQUIRE(1 == str<>("a").char_count());
            REQUIRE(2 == str<>("a\xcf\xbf").char_count());
            REQUIRE(2 == str<>("a\xef\xbf\x8f").char_count());
            REQUIRE(2 == str<>("a\xf7\xbf\x8f\xa5").char_count());
            REQUIRE(2 == str<>("a\xfb\xbf\x8f\xa5\x9a").char_count());
        }
    }

    SECTION("From UTF-8") {
        wstr<> s;

        SECTION("One byte") {
            s.convert("0123456789");
            REQUIRE(s.equals(L"0123456789"));

            s.convert("\x01\x7f");
            REQUIRE(s.equals(L"\x01\x7f"));

            s.convert("AaBbCc");
            REQUIRE(s.equals(L"AaBbCc"));
        }

        SECTION("Two bytes") {
            s.convert("\xc2\x80");
            REQUIRE(s.equals(L"\x0080"));

            s.convert("\xdf\xbf");
            REQUIRE(s.equals(L"\x07ff"));
        }

        SECTION("Three bytes") {
            s.convert("\xe0\xa0\x80");
            REQUIRE(s.equals(L"\x0800"));

            s.convert("\xef\xbf\xbf");
            REQUIRE(s.equals(L"\xffff"));
        }

        SECTION("Four bytes") {
            s.convert("\xf0\x90\x80\x80");
            REQUIRE(s.equals(L"\xd800\xdc00"));

            s.convert("\xf4\x8f\xbf\xbf");
            REQUIRE(s.equals(L"\xdbff\xdfff"));
        }

        SECTION("char_count()") {
            REQUIRE(wstr<>(L"\xdbff\xdfff").char_count() == 1);
            REQUIRE(wstr<>(L"\xd800\xdc00").char_count() == 1);
        }
    }
}
