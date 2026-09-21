// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"

//------------------------------------------------------------------------------
extern bool g_color_emoji;

//------------------------------------------------------------------------------
TEST_CASE("wcwidth_iter")
{
    SECTION("widths")
    {
        struct testcase
        {
            uint32_t cols;
            const char* str;
            bool emoji;
        };

        static const testcase c_testcases[] =
        {
            { 1,    u8"a" },
            { 2,    u8"ab" },
            { 3,    u8"abc" },
            { 4,    u8"abcd" },
            { 5,    u8"abcd" },
            { 5,    u8"ÀΘЙ≋☑" },
            { 2,    u8"✔️" },
            { 3,    u8"✔️ " },
            { 3,    u8"✔️x" },
            { 4,    u8"y✔️x" },
            { 2,    u8"✔️", true },
            { 3,    u8"✔️ ", true },
            { 3,    u8"✔️x", true },
            { 4,    u8"y✔️x", true },
            { 2,    u8"☘️" },
            { 2,    u8"☘️", true },
            { 1,    u8"☘" },
            { 1,    u8"☘", true },
        };

        const bool old = g_color_emoji;

        uint32_t index = 0;
        for (auto const& t : c_testcases)
        {
            uint32_t cols = 0;

            g_color_emoji = t.emoji;

            tib::cstring s(t.str);

            wcwidth_iter iter(s.c_str());
            while (iter.next())
                cols += iter.character_wcwidth_onectrl();

            auto callback = [&] () {
                DWORD written;
                WCHAR buffer[128];
                tib::cstring_t<WCHAR> ws;
                tib::to_utf16(t.str, tib::c_auto_length, ws);
                swprintf_s(buffer, _countof(buffer),
                        L"   index:  %u\n     str:  \"%s\"\n    cols:  %u\nexpected:  %u",
                        index, ws.c_str(), cols, t.cols);
                WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buffer, DWORD(wcslen(buffer)), &written, nullptr);
            };

            REQUIRE(t.cols == cols, callback);

            ++index;
        }

        g_color_emoji = old;
    }

    SECTION("unnext ASCII")
    {
        wcwidth_iter iter("abcd");

        REQUIRE(iter.next() == 'a');
        REQUIRE(iter.next() == 'b');
        REQUIRE(iter.character_wcwidth_signed() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'b');
        REQUIRE(*iter.get_pointer() == 'c');

        iter.unnext();
        REQUIRE(iter.character_wcwidth_signed() == 0);
        REQUIRE(iter.character_length() == 0);
        REQUIRE(*iter.character_pointer() == 'b');
        REQUIRE(*iter.get_pointer() == 'b');

        REQUIRE(iter.next() == 'b');
        REQUIRE(iter.character_wcwidth_signed() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'b');
        REQUIRE(*iter.get_pointer() == 'c');

        REQUIRE(iter.next() == 'c');
        REQUIRE(iter.character_wcwidth_signed() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'c');
        REQUIRE(*iter.get_pointer() == 'd');
    }


    SECTION("unnext emoji")
    {
        const bool old = g_color_emoji;
        g_color_emoji = true;

        tib::cstring s(u8"a☘️cd");
        wcwidth_iter iter(s.c_str());

        REQUIRE(iter.next() == 'a');
        REQUIRE(iter.next() == 0x2618);
        REQUIRE(iter.character_wcwidth_signed() == 2);
        REQUIRE(iter.character_length() == 6);
        REQUIRE(BYTE(*iter.character_pointer()) == 0xe2);
        REQUIRE(*iter.get_pointer() == 'c');

        iter.unnext();
        REQUIRE(iter.character_wcwidth_signed() == 0);
        REQUIRE(iter.character_length() == 0);
        REQUIRE(BYTE(*iter.character_pointer()) == 0xe2);
        REQUIRE(BYTE(*iter.get_pointer()) == 0xe2);

        REQUIRE(iter.next() == 0x2618);
        REQUIRE(iter.character_wcwidth_signed() == 2);
        REQUIRE(iter.character_length() == 6);
        REQUIRE(BYTE(*iter.character_pointer()) == 0xe2);
        REQUIRE(*iter.get_pointer() == 'c');

        REQUIRE(iter.next() == 'c');
        REQUIRE(iter.character_wcwidth_signed() == 1);
        REQUIRE(iter.character_length() == 1);
        REQUIRE(*iter.character_pointer() == 'c');
        REQUIRE(*iter.get_pointer() == 'd');

        g_color_emoji = false;
    }
}
