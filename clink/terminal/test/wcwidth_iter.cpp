// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <terminal/ecma48_iter.h>
#include <terminal/wcwidth.h>

//------------------------------------------------------------------------------
extern bool g_color_emoji;

//------------------------------------------------------------------------------
TEST_CASE("wcwidth_iter")
{
    SECTION("widths")
    {
        struct testcase
        {
            unsigned int cols;
            const WCHAR* str;
            bool emoji;
        };

        static const testcase c_testcases[] =
        {
            { 1,    L"a" },
            { 2,    L"ab" },
            { 3,    L"abc" },
            { 4,    L"abcd" },
            { 5,    L"abcd" },
            { 5,    L"ÀΘЙ≋☑" },
            { 1,    L"✔️" },
            { 2,    L"✔️ " },
            { 2,    L"✔️x" },
            { 3,    L"y✔️x" },
            { 2,    L"✔️", true },
            { 3,    L"✔️ ", true },
            { 3,    L"✔️x", true },
            { 4,    L"y✔️x", true },
            { 1,    L"☘️" },
            { 2,    L"☘️", true },
            { 1,    L"☘" },
            { 1,    L"☘", true },
        };

        const bool old = g_color_emoji;

        unsigned int index = 0;
        for (auto const& t : c_testcases)
        {
            unsigned int cols = 0;

            g_color_emoji = t.emoji;

            str<> s(t.str);

            wcwidth_iter iter(s.c_str());
            while (iter.next())
                cols += iter.character_wcwidth_onectrl();

            REQUIRE(t.cols == cols, [&] () {
                DWORD written;
                WCHAR buffer[128];
                swprintf_s(buffer, _countof(buffer),
                        L"   index:  %u\n     str:  \"%s\"\n    cols:  %u\nexpected:  %u",
                        index, t.str, cols, t.cols);
                WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buffer, DWORD(wcslen(buffer)), &written, nullptr);
            });

            ++index;
        }

        g_color_emoji = old;
    }

    SECTION("unnext ASCII")
    {
        wcwidth_iter iter("abcd");

        REQUIRE(iter.next() == 'a');
        REQUIRE(iter.next() == 'b');
        REQUIRE(iter.character_wcwidth() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'b');
        REQUIRE(*iter.get_pointer() == 'c');

        iter.unnext();
        REQUIRE(iter.character_wcwidth() == 0);
        REQUIRE(iter.character_length() == 0);
        REQUIRE(*iter.character_pointer() == 'b');
        REQUIRE(*iter.get_pointer() == 'b');

        REQUIRE(iter.next() == 'b');
        REQUIRE(iter.character_wcwidth() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'b');
        REQUIRE(*iter.get_pointer() == 'c');

        REQUIRE(iter.next() == 'c');
        REQUIRE(iter.character_wcwidth() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'c');
        REQUIRE(*iter.get_pointer() == 'd');
    }


    SECTION("unnext emoji")
    {
        const bool old = g_color_emoji;
        g_color_emoji = true;

        str<> s(L"a☘️cd");
        wcwidth_iter iter(s.c_str());

        REQUIRE(iter.next() == 'a');
        REQUIRE(iter.next() == 0x2618);
        REQUIRE(iter.character_wcwidth() == 2);
        REQUIRE(iter.character_length() == 6);
        REQUIRE(BYTE(*iter.character_pointer()) == 0xe2);
        REQUIRE(*iter.get_pointer() == 'c');

        iter.unnext();
        REQUIRE(iter.character_wcwidth() == 0);
        REQUIRE(iter.character_length() == 0);
        REQUIRE(BYTE(*iter.character_pointer()) == 0xe2);
        REQUIRE(BYTE(*iter.get_pointer()) == 0xe2);

        REQUIRE(iter.next() == 0x2618);
        REQUIRE(iter.character_wcwidth() == 2);
        REQUIRE(iter.character_length() == 6);
        REQUIRE(BYTE(*iter.character_pointer()) == 0xe2);
        REQUIRE(*iter.get_pointer() == 'c');

        REQUIRE(iter.next() == 'c');
        REQUIRE(iter.character_wcwidth() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'c');
        REQUIRE(*iter.get_pointer() == 'd');

        g_color_emoji = false;
    }
}
