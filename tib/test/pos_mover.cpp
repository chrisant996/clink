// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "maybe_windows.h"
#include "test.h"
#include "tib.h"

struct grapheme_range
{
    tib::textpos_t      begin;
    tib::textpos_t      end;
};

static void __check_move(const char* text, size_t len, tib::textpos_t initial,
                         bool forward, bool word, tib::textpos_t expected_pos,
                         tib::textpos_t expected_moved, uint32_t lineno)
{
    tib::textpos_t pos = initial;
    const tib::textpos_t moved = tib::pos_mover(text, len, pos, forward, word);

    REQUIRE(pos == expected_pos, [&]()
    {
        printf(" initial=%d forward=%d word=%d expected_pos=%d actual_pos=%d (line %u)\n",
               initial, forward, word, expected_pos, pos, lineno);
    });
    REQUIRE(moved == expected_moved, [&]()
    {
        printf(" initial=%d forward=%d word=%d expected_moved=%d actual_moved=%d (line %u)\n",
               initial, forward, word, expected_moved, moved, lineno);
    });
}

#define check_move(t, l, i, f, w, ep, em) __check_move(t, l, i, f, w, ep, em, __LINE__)

TEST_CASE("Position mover")
{
    // Byte offsets and grapheme boundaries:
    //
    //   0  1  2        4        7                   18  19  20  21
    //   A  _  e-acute  e+acute  woman-technologist  _   \t  Z
    //
    // The Unicode word from offsets 2 through 18 contains a two-byte
    // codepoint, a grapheme with a combining mark, and a ZWJ emoji sequence.
    static const char text[] =
        "A "
        "\xc3\xa9"                      // U+00E9 LATIN SMALL LETTER E WITH ACUTE.
        "e\xcc\x81"                     // e + U+0301 COMBINING ACUTE ACCENT.
        "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"  // U+1F469 ZWJ U+1F4BB.
        " \tZ";
    static_assert(sizeof(text) - 1 == 21);

    static const tib::textpos_t boundaries[] =
    {
        0, 1, 2, 4, 7, 18, 19, 20, 21,
    };
    static const grapheme_range unicode_graphemes[] =
    {
        { 2, 4 },       // Two-byte codepoint.
        { 4, 7 },       // Base plus a two-byte combining mark.
        { 7, 18 },      // Three codepoints forming one ZWJ emoji grapheme.
    };

    SECTION("One grapheme from grapheme boundaries")
    {
        for (size_t i = 0; i + 1 < std::size(boundaries); ++i)
        {
            const tib::textpos_t begin = boundaries[i];
            const tib::textpos_t end = boundaries[i + 1];
            check_move(text, sizeof(text) - 1, begin, true, false, end, end - begin);
            check_move(text, sizeof(text) - 1, end, false, false, begin, end - begin);
        }
    }

    SECTION("One grapheme from every interior byte")
    {
        for (const auto& grapheme : unicode_graphemes)
        {
            for (tib::textpos_t initial = grapheme.begin + 1; initial < grapheme.end; ++initial)
            {
                // Direction determines which boundary an interior position
                // is normalized to before moving across the whole grapheme.
                check_move(text, sizeof(text) - 1, initial, true, false, grapheme.end, grapheme.end - grapheme.begin);
                check_move(text, sizeof(text) - 1, initial, false, false, grapheme.begin, grapheme.end - grapheme.begin);
            }
        }
    }

    SECTION("One word from word and whitespace boundaries")
    {
        // Forward movement consumes leading whitespace followed by one word.
        check_move(text, sizeof(text) - 1, 0, true, true, 1, 1);
        check_move(text, sizeof(text) - 1, 1, true, true, 18, 17);
        check_move(text, sizeof(text) - 1, 2, true, true, 18, 16);
        check_move(text, sizeof(text) - 1, 4, true, true, 18, 14);
        check_move(text, sizeof(text) - 1, 7, true, true, 18, 11);
        check_move(text, sizeof(text) - 1, 18, true, true, 21, 3);
        check_move(text, sizeof(text) - 1, 19, true, true, 21, 2);
        check_move(text, sizeof(text) - 1, 20, true, true, 21, 1);
        check_move(text, sizeof(text) - 1, 21, true, true, 21, 0);

        // Backward movement consumes leading whitespace in the backward
        // direction followed by one word.
        check_move(text, sizeof(text) - 1, 21, false, true, 20, 1);
        check_move(text, sizeof(text) - 1, 20, false, true, 2, 18);
        check_move(text, sizeof(text) - 1, 19, false, true, 2, 17);
        check_move(text, sizeof(text) - 1, 18, false, true, 2, 16);
        check_move(text, sizeof(text) - 1, 7, false, true, 2, 5);
        check_move(text, sizeof(text) - 1, 4, false, true, 2, 2);
        check_move(text, sizeof(text) - 1, 2, false, true, 0, 2);
        check_move(text, sizeof(text) - 1, 1, false, true, 0, 1);
        check_move(text, sizeof(text) - 1, 0, false, true, 0, 0);
    }

    SECTION("One word from every interior byte")
    {
        for (const auto& grapheme : unicode_graphemes)
        {
            for (tib::textpos_t initial = grapheme.begin + 1; initial < grapheme.end; ++initial)
            {
                check_move(text, sizeof(text) - 1, initial, true, true, 18, 18 - grapheme.begin);
                check_move(text, sizeof(text) - 1, initial, false, true, 2, grapheme.end - 2);
            }
        }
    }

    SECTION("Non-ASCII punctuation is a word boundary")
    {
        static const char punctuation[] =
            "A"
            "\xc2\xa2"                  // U+00A2 CENT SIGN.
            "B";
        static_assert(sizeof(punctuation) - 1 == 4);

        check_move(punctuation, sizeof(punctuation) - 1, 0, true, true, 1, 1);
        check_move(punctuation, sizeof(punctuation) - 1, 1, true, true, 4, 3);
        check_move(punctuation, sizeof(punctuation) - 1, 4, false, true, 3, 1);
        check_move(punctuation, sizeof(punctuation) - 1, 3, false, true, 0, 3);
    }
}
