// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "str_iter.h"
#include <initializer_list>

struct utf8_step
{
    char32_t            value;
    size_t              offset;
};

static void check_utf8(const char* input, size_t length, std::initializer_list<utf8_step> expected)
{
    str_iter iter(input, length);

    for (const auto& step : expected)
    {
        REQUIRE(iter.next() == step.value);
        REQUIRE(iter.get_pointer() == input + step.offset);
    }

    REQUIRE(!iter.more());
    REQUIRE(iter.next() == 0);
}

template <size_t InputSize>
static void check_utf8(const char (&input)[InputSize], std::initializer_list<utf8_step> expected)
{
    check_utf8(input, InputSize - 1, expected);
}

TEST_CASE("Invalid UTF8")
{
    SECTION("Invalid leading bytes")
    {
        const char invalid_c1[] = { char(0xc1), 'a', '\0' };
        check_utf8(invalid_c1, { { 0xfffd, 1 }, { 'a', 2 } });

        for (uint16_t byte = 0xf5; byte <= 0xff; ++byte)
        {
            const char input[] = { char(byte), 'a', '\0' };
            check_utf8(input, { { 0xfffd, 1 }, { 'a', 2 } });
        }
    }

    SECTION("Unexpected continuation bytes")
    {
        for (uint16_t byte = 0x80; byte <= 0xbf; ++byte)
        {
            const char input[] = { char(byte), 'a', '\0' };
            check_utf8(input, { { 0xfffd, 1 }, { 'a', 2 } });
        }

        const char consecutive[] = { char(0x80), char(0xbf), 'a', '\0' };
        check_utf8(consecutive, { { 0xfffd, 1 }, { 0xfffd, 2 }, { 'a', 3 } });
    }

    SECTION("Non-continuation bytes end incomplete sequences")
    {
        const char two_byte[] = { char(0xc2), 'a', '\0' };
        check_utf8(two_byte, { { 0xfffd, 1 }, { 'a', 2 } });

        const char three_byte_after_lead[] = { char(0xe1), 'a', '\0' };
        check_utf8(three_byte_after_lead, { { 0xfffd, 1 }, { 'a', 2 } });

        const char three_byte_after_one_continuation[] =
            { char(0xe1), char(0x80), 'a', '\0' };
        check_utf8(three_byte_after_one_continuation,
            { { 0xfffd, 2 }, { 'a', 3 } });

        const char four_byte_after_lead[] = { char(0xf1), 'a', '\0' };
        check_utf8(four_byte_after_lead, { { 0xfffd, 1 }, { 'a', 2 } });

        const char four_byte_after_one_continuation[] =
            { char(0xf1), char(0x80), 'a', '\0' };
        check_utf8(four_byte_after_one_continuation,
            { { 0xfffd, 2 }, { 'a', 3 } });

        const char four_byte_after_two_continuations[] =
            { char(0xf1), char(0x80), char(0x80), 'a', '\0' };
        check_utf8(four_byte_after_two_continuations,
            { { 0xfffd, 3 }, { 'a', 4 } });

        const char followed_by_valid_utf8[] =
            { char(0xe1), char(0xc2), char(0xa2), 'a', '\0' };
        check_utf8(followed_by_valid_utf8,
            { { 0xfffd, 1 }, { 0x00a2, 3 }, { 'a', 4 } });
    }

    SECTION("Truncated sequences")
    {
        const char two_byte[] = { char(0xc2) };
        check_utf8(two_byte, sizeof(two_byte), { { 0xfffd, 1 } });

        const char zero_accumulator_three_byte[] = { char(0xe0) };
        check_utf8(zero_accumulator_three_byte, sizeof(zero_accumulator_three_byte), { { 0xfffd, 1 } });

        const char three_byte[] = { char(0xe1), char(0x80) };
        check_utf8(three_byte, sizeof(three_byte), { { 0xfffd, 2 } });

        const char zero_accumulator_four_byte[] = { char(0xf0) };
        check_utf8(zero_accumulator_four_byte, sizeof(zero_accumulator_four_byte), { { 0xfffd, 1 } });

        const char four_byte[] = { char(0xf1), char(0x80), char(0x80) };
        check_utf8(four_byte, sizeof(four_byte), { { 0xfffd, 3 } });
    }

    SECTION("Overlong encodings")
    {
        const char two_byte_low[] = { char(0xc0), char(0x81), 'a', '\0' };
        check_utf8(two_byte_low, { { 0xfffd, 1 }, { 0xfffd, 2 }, { 'a', 3 } });

        const char two_byte_high[] = { char(0xc0), char(0xbf), 'a', '\0' };
        check_utf8(two_byte_high, { { 0xfffd, 1 }, { 0xfffd, 2 }, { 'a', 3 } });

        const char three_byte_low[] =
            { char(0xe0), char(0x80), char(0x80), 'a', '\0' };
        check_utf8(three_byte_low,
            { { 0xfffd, 1 }, { 0xfffd, 2 }, { 0xfffd, 3 }, { 'a', 4 } });

        const char three_byte_high[] =
            { char(0xe0), char(0x9f), char(0xbf), 'a', '\0' };
        check_utf8(three_byte_high,
            { { 0xfffd, 1 }, { 0xfffd, 2 }, { 0xfffd, 3 }, { 'a', 4 } });

        const char four_byte_low[] =
            { char(0xf0), char(0x80), char(0x80), char(0x80), 'a', '\0' };
        check_utf8(four_byte_low,
            { { 0xfffd, 1 }, { 0xfffd, 2 }, { 0xfffd, 3 }, { 0xfffd, 4 }, { 'a', 5 } });

        const char four_byte_high[] =
            { char(0xf0), char(0x8f), char(0xbf), char(0xbf), 'a', '\0' };
        check_utf8(four_byte_high,
            { { 0xfffd, 1 }, { 0xfffd, 2 }, { 0xfffd, 3 }, { 0xfffd, 4 }, { 'a', 5 } });

        // This project intentionally accepts the modified UTF-8 encoding of NUL.
        const char modified_nul[] = { char(0xc0), char(0x80), 'a', '\0' };
        check_utf8(modified_nul, { { 0, 2 }, { 'a', 3 } });
    }

    SECTION("Surrogate code points")
    {
        // The replacement policy in str_iter is that otherwise-well-formed
        // UTF8 byte sequences that decode to a surrogate should produce only
        // a single replacement character for the whole surrogate.  Otherwise
        // things get a bit crazy.

        const char first_surrogate[] =
            { char(0xed), char(0xa0), char(0x80), 'a', '\0' };
        check_utf8(first_surrogate,
            { /*{ 0xfffd, 1 }, { 0xfffd, 2 },*/ { 0xfffd, 3 }, { 'a', 4 } });

        const char last_surrogate[] =
            { char(0xed), char(0xbf), char(0xbf), 'a', '\0' };
        check_utf8(last_surrogate,
            { /*{ 0xfffd, 1 }, { 0xfffd, 2 },*/ { 0xfffd, 3 }, { 'a', 4 } });
    }

    SECTION("Code points above Unicode range")
    {
        const char input[] =
            { char(0xf4), char(0x90), char(0x80), char(0x80), 'a', '\0' };
        check_utf8(input,
            { { 0xfffd, 1 }, { 0xfffd, 2 }, { 0xfffd, 3 }, { 0xfffd, 4 }, { 'a', 5 } });
    }

    SECTION("Valid boundary encodings")
    {
        const char first_two_byte[] = { char(0xc2), char(0x80), 'a', '\0' };
        check_utf8(first_two_byte, { { 0x0080, 2 }, { 'a', 3 } });

        const char last_two_byte[] = { char(0xdf), char(0xbf), 'a', '\0' };
        check_utf8(last_two_byte, { { 0x07ff, 2 }, { 'a', 3 } });

        const char first_three_byte[] =
            { char(0xe0), char(0xa0), char(0x80), 'a', '\0' };
        check_utf8(first_three_byte, { { 0x0800, 3 }, { 'a', 4 } });

        const char before_surrogates[] =
            { char(0xed), char(0x9f), char(0xbf), 'a', '\0' };
        check_utf8(before_surrogates, { { 0xd7ff, 3 }, { 'a', 4 } });

        const char after_surrogates[] =
            { char(0xee), char(0x80), char(0x80), 'a', '\0' };
        check_utf8(after_surrogates, { { 0xe000, 3 }, { 'a', 4 } });

        const char first_four_byte[] =
            { char(0xf0), char(0x90), char(0x80), char(0x80), 'a', '\0' };
        check_utf8(first_four_byte, { { 0x10000, 4 }, { 'a', 5 } });

        const char last_four_byte[] =
            { char(0xf4), char(0x8f), char(0xbf), char(0xbf), 'a', '\0' };
        check_utf8(last_four_byte, { { 0x10ffff, 4 }, { 'a', 5 } });
    }
}
