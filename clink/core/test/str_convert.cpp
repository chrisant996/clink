// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include <core/str.h>
#include <core/str_iter.h>

//------------------------------------------------------------------------------
TEST_CASE("Wide character/UTF-8 conversion")
{
    SECTION("To UTF-8")
    {
        str<> s;

        SECTION("One byte")
        {
            s.from_utf16(L"0123456789");
            REQUIRE(s.equals("0123456789"));

            s.from_utf16(L"\x01\x7f");
            REQUIRE(s.equals("\x01\x7f"));

            s.from_utf16(L"AaBbCc");
            REQUIRE(s.equals("AaBbCc"));
        }

        SECTION("Two bytes")
        {
            s.from_utf16(L"\x0080");
            REQUIRE(s.equals("\xc2\x80"));

            s.from_utf16(L"\x07ff");
            REQUIRE(s.equals("\xdf\xbf"));
        }

        SECTION("Three bytes")
        {
            s.from_utf16(L"\x0800");
            REQUIRE(s.equals("\xe0\xa0\x80"));

            s.from_utf16(L"\xffff");
            REQUIRE(s.equals("\xef\xbf\xbf"));
        }

        SECTION("Four bytes")
        {
            s.from_utf16(L"\xd800\xdc00");
            REQUIRE(s.equals("\xf0\x90\x80\x80"));

            s.from_utf16(L"\xdbff\xdfff");
            REQUIRE(s.equals("\xf4\x8f\xbf\xbf"));
        }

        SECTION("char_count()")
        {
            REQUIRE(1 == str<>("a").char_count());
            REQUIRE(2 == str<>("a\xcf\xbf").char_count());
            REQUIRE(2 == str<>("a\xef\xbf\x8f").char_count());
            REQUIRE(2 == str<>("a\xf7\xbf\x8f\xa5").char_count());
            REQUIRE(2 == str<>("a\xfb\xbf\x8f\xa5\x9a").char_count());
        }

        SECTION("Growable")
        {
            str<4, true> t;
            t.from_utf16(L"0123456789");
            REQUIRE(t.length() == 10);
        }

        SECTION("Not growable")
        {
            str<4, false> t;
            t.from_utf16(L"0123456789");
            REQUIRE(t.length() == 3);
        }

        SECTION("Stream")
        {
            wstr_iter iter(L"01234567");
            char out[6];

            REQUIRE(to_utf8(out, 6, iter) == 5);
            REQUIRE(out[0] == '0');
            REQUIRE(iter.more());

            REQUIRE(to_utf8(out, 6, iter) == 3);
            REQUIRE(out[0] == '5');
            REQUIRE(!iter.more());
        }
    }

    SECTION("From UTF-8")
    {
        wstr<> s;

        SECTION("One byte")
        {
            s.from_utf8("0123456789");
            REQUIRE(s.equals(L"0123456789"));

            s.from_utf8("\x01\x7f");
            REQUIRE(s.equals(L"\x01\x7f"));

            s.from_utf8("AaBbCc");
            REQUIRE(s.equals(L"AaBbCc"));
        }

        SECTION("Two bytes")
        {
            s.from_utf8("\xc2\x80");
            REQUIRE(s.equals(L"\x0080"));

            s.from_utf8("\xdf\xbf");
            REQUIRE(s.equals(L"\x07ff"));
        }

        SECTION("Three bytes")
        {
            s.from_utf8("\xe0\xa0\x80");
            REQUIRE(s.equals(L"\x0800"));

            s.from_utf8("\xef\xbf\xbf");
            REQUIRE(s.equals(L"\xffff"));
        }

        SECTION("Four bytes")
        {
            s.from_utf8("\xf0\x90\x80\x80");
            REQUIRE(s.equals(L"\xd800\xdc00"));

            s.from_utf8("\xf4\x8f\xbf\xbf");
            REQUIRE(s.equals(L"\xdbff\xdfff"));
        }

        SECTION("char_count()")
        {
            REQUIRE(wstr<>(L"\xdbff\xdfff").char_count() == 1);
            REQUIRE(wstr<>(L"\xd800\xdc00").char_count() == 1);
        }

        SECTION("Growable")
        {
            wstr<4, true> t;
            t.from_utf8("0123456789");
            REQUIRE(t.length() == 10);
        }

        SECTION("Not growable")
        {
            wstr<4, false> t;
            t.from_utf8("0123456789");
            REQUIRE(t.length() == 3);
        }

        SECTION("Stream")
        {
            str_iter iter("01234567");
            wchar_t out[6];

            REQUIRE(to_utf16(out, 6, iter) == 5);
            REQUIRE(out[0] == '0');
            REQUIRE(iter.more());

            REQUIRE(to_utf16(out, 6, iter) == 3);
            REQUIRE(out[0] == '5');
            REQUIRE(!iter.more());
        }
    }
}
