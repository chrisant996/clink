// Copyright (c) 2026 Christopher Antos
// Portions Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "tib_base.h"
#include "str_iter.h"
#include <vector> // For std::size.

template <>
char32_t str_iter_impl<char>::next()
{
    if (!more())
        return 0;

    // https://en.wikipedia.org/wiki/UTF-8
    //
    //  - Bytes that never appear in UTF-8: 0xC0, 0xC1, 0xF5–0xFF,
    //  - A "continuation byte" (0x80–0xBF) at the start of a character,
    //  - A non-continuation byte (or the string ending) before the end of a
    //    character.
    //  - An overlong encoding (0xE0 followed by less than 0xA0, or 0xF0
    //    followed by less than 0x90).
    //  - A 4-byte sequence that decodes to a value greater than U+10FFFF
    //    (0xF4 followed by 0x90 or greater).
    //
    // HOWEVER, overlong 0xC0 0x80 should be allowed for U+0000.

    uint32_t ax = 0;
    uint8_t expected = 0;
    uint8_t length = 0;
    int8_t invalid = 0;
    do
    {
        if (invalid)
        {
            // -1 == preceding data was invalid.
            // 1 == deferred reporting of data that has now become preceding.
            return 0xFFFD;
        }

        const uint8_t c = uint8_t(*m_ptr);

        if (c <= 0x7F)
        {
            if (!(length == expected))
            {
                // A non-continuation byte (or the string ending) cannot
                // appear before the end of a character.
invalid_preceding_data:
                invalid = -1;
                ax = 0xFFFD;
            }
            else
            {
                // An ASCII byte.
                ++m_ptr;
                return c;
            }
        }
        else if (c >= 0xF5 || c == 0xC1)
        {
            // Bytes that never appear in UTF-8: 0xC1, 0xF5–0xFF.
            if (!(length == expected))
                goto invalid_preceding_data;
invalid_current_data:
            ++m_ptr;
            return 0xFFFD;
        }
        else if (c >= 0b11110000)
        {
            // A non-continuation byte (or the string ending) cannot appear
            // before the end of a character.
            if (!(length == expected))
                goto invalid_preceding_data;

            // Start a four byte sequence.
            expected = 4;
            ++m_ptr;
            length = 1;
            ax = c & 0b00000111;
        }
        else if (c >= 0b11100000)
        {
            // A non-continuation byte (or the string ending) cannot appear before
            // the end of a character.
            if (!(length == expected))
                goto invalid_preceding_data;

            // Start a three byte sequence.
            expected = 3;
            ++m_ptr;
            length = 1;
            ax = c & 0b00001111;
        }
        else if (c >= 0b11000000)
        {
            // A non-continuation byte (or the string ending) cannot appear before
            // the end of a character.
            if (!(length == expected))
                goto invalid_preceding_data;

            // Start a two byte sequence.
            expected = 2;
            ++m_ptr;
            length = 1;
            ax = c & 0b00011111;
        }
        else
        {
            // Continuation byte.
            assert(c >= 0b10000000);

            // A "continuation byte" (0x80–0xBF) cannot appear at the start of a
            // character.
            if (length == expected)
                goto invalid_current_data;

            // Detect a 4-byte sequence that decodes to a value greater than
            // U+10FFFF (0xF4 followed by 0x90 or greater).
            if (ax == 4 && c >= 0x90 && expected == 4 && length == 1)
                goto invalid_preceding_data;

            // Detect overlong encodings.
            if (ax == 0)
            {
                switch (expected)
                {
                case 3:
                    // 0xE0 followed by less than 0xA0.
                    if (c < 0xA0 && length == 1)
                        goto invalid_preceding_data;
                    break;
                case 4:
                    // 0xF0 followed by less than 0x90.
                    if (c < 0x90 && length == 1)
                        goto invalid_preceding_data;
                    break;
                case 2:
                    // 0xC0 followed by 0x80 is an overlong encoding for U+0000,
                    // which is accepted so that U+0000 can be encoded without
                    // using any NUL bytes.  But no other use of 0xC0 is allowed.
                    if (length == 1 && c != 0x80)
                        goto invalid_preceding_data;
                    break;
                }
            }

            ++length;
            ++m_ptr;
            ax = (ax << 6) | (c & 0b01111111);
            if (length == expected)
            {
                // Detect surrogates.
                if (expected == 3 && uint32_t(ax - 0xD800) <= 0x07FF)
                    return 0xFFFD;
                return ax;
            }
        }

        assert(expected);
        assert(length < expected);
    }
    while (more());

    // An incomplete encoding is invalid.
    assert(expected);
    assert(length < expected);
    return 0xFFFD;
}

template <>
size_t str_iter_impl<char>::length() const
{
    return size_t((m_ptr <= m_end) ? m_end - m_ptr : strlen(m_ptr));
}

#ifdef _WIN32
template <>
char32_t str_iter_impl<WCHAR>::next()
{
    int32_t c;
    int32_t ax = 0;

    while (more() && (c = *m_ptr++))
    {
        // Decode surrogate pairs.
        if ((c & 0xfc00) == 0xd800)
        {
            if (!more() || (*m_ptr & 0xfc00) != 0xdc00)         // Invalid.
                return 0xFFFD;
            ax = c << 10;
            continue;
        }
        else if ((c & 0xfc00) == 0xdc00)
        {
            if (ax < (1 << 10))                                 // Invalid.
                return 0xFFFD;
            c = ax + c - 0x35fdc00;
            ax = 0;
        }
        else
        {
            if (ax)                                             // Invalid.
               return 0xFFFD;
        }
        return c;
    }

    if (ax)                                                     // Invalid.
        return 0xFFFD;
    return 0;
}
#endif

template <>
size_t str_iter_impl<wchar_t>::length() const
{
    return size_t((m_ptr <= m_end) ? m_end - m_ptr : wcslen(m_ptr));
}
