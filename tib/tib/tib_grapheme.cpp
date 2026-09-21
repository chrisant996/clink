// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_grapheme.h"
#include "wcwidth.h"
#include <assert.h>

namespace tib {

const char c_replacement_character[] = "\xef\xbf\xbd";
const uint32_t c_replacement_character_length = 3;
static_assert(c_replacement_character_length == std::size(c_replacement_character) - 1);

uint32_t backward_one_grapheme(const char* const s, const size_t len, const uint32_t pos, uint16_t* width)
{
    assert(pos <= len);

    if (width)
        *width = 0;
    if (pos <= 0)
        return 0;

    uint32_t backward = 0;
    uint32_t previous_nonzero = 0;
    bool have_nonzero = false;
    uint32_t walk = pos;
    while (walk)
    {
        // Find beginning of preceding UTF8 codepoint.
        do
            --walk;
        while (walk > 0 && (uint8_t(s[walk]) & 0xc0) == 0x80);

        // Decode the codepoint.
        // BUGBUG: this does not handle invalid UTF8 correctly.
        const uint8_t lead = uint8_t(s[walk]);
        char32_t codepoint;
        if (lead < 0x80)
            codepoint = lead;
        else if (lead < 0xe0)
            codepoint = ((lead & 0x1f) << 6) |
                        (uint8_t(s[walk + 1]) & 0x3f);
        else if (lead < 0xf0)
            codepoint = ((lead & 0x0f) << 12) |
                        ((uint8_t(s[walk + 1]) & 0x3f) << 6) |
                        (uint8_t(s[walk + 2]) & 0x3f);
        else
            codepoint = ((lead & 0x07) << 18) |
                        ((uint8_t(s[walk + 1]) & 0x3f) << 12) |
                        ((uint8_t(s[walk + 2]) & 0x3f) << 6) |
                        (uint8_t(s[walk + 3]) & 0x3f);

        // Keep backing up until two adjacent, independent non-zero width
        // codepoints, and then switch to parsing forward.  Continuations and
        // regional indicators require parsing from farther back.
        const bool continuation = ((wcwidth(codepoint) == 0) ||
                                   (codepoint >= 0x1f1e6 && codepoint <= 0x1f1ff) || // Regional indicator.
                                   (g_color_emoji && is_variant_selector(codepoint)));
        if (continuation)
            have_nonzero = false;
        else if (have_nonzero)
        {
            backward = previous_nonzero;
            break;
        }
        else
        {
            previous_nonzero = walk;
            have_nonzero = true;
        }
    }

    assert(backward < pos);
    assert(!width || !*width);
    walk = backward;

    wcwidth_iter iter(s + walk, len - walk);
    while (iter.next())
    {
        assert(s + walk == iter.character_pointer());
        const uint32_t clen = iter.character_length();
        if (walk + clen >= pos)
        {
            if (width)
                *width = iter.character_wcwidth_twoctrl();
            break;
        }
        walk += clen;
    }

    return walk;
}

uint32_t forward_one_grapheme(const char* s, size_t len, uint32_t pos, uint16_t* width)
{
    assert(pos <= len);

    if (width)
        *width = 0;
    if (pos >= len)
        return uint32_t(len);

    wcwidth_iter iter(s + pos, len - pos);
    if (!iter.next())
        return pos;

    if (width)
        *width = uint16_t(iter.character_wcwidth_twoctrl());
    return pos + iter.character_length();
}

size_t parse_graphemes(const char* s, const size_t len, const uint32_t pos, std::vector<grapheme_info>& out)
{
    out.clear();

    wcwidth_iter iter(s, len);
    uint32_t char_index = 0;
    size_t index_pos = 0;
    while (iter.next())
    {
        if (char_index <= pos)
            index_pos = out.size();
        const uint32_t char_length = iter.character_length();
        out.push_back(grapheme_info { char_index, char_length, uint16_t(iter.character_wcwidth_onectrl()) });
        char_index += char_length;
    }
    assert(char_index == len);

    return index_pos;
}

} // namespace tib
