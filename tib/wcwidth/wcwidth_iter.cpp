// Copyright (c) 2023,2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "tib_base.h"
#include "wcwidth.h"
#include <assert.h>

extern "C" uint32_t __wcswidth(const char* s, size_t len)
{
    uint32_t count = 0;

    wcwidth_iter iter(s, len);
    while (iter.next())
        count += iter.character_wcwidth_onectrl();

    return count;
}

extern "C" uint32_t __wcswidth_expandctrl(const char* s, size_t len)
{
    uint32_t count = 0;

    wcwidth_iter iter(s, len);
    while (iter.next())
        count += iter.character_wcwidth_twoctrl();

    return count;
}

extern "C" uint32_t cell_count(const char* s, size_t len)
{
    // REVIEW: Parse and ignore ECMA48 escape sequences?
    return __wcswidth(s, len);
}

wcwidth_iter::wcwidth_iter(const char* s, size_t len)
: m_iter(s, len)
{
    m_chr_ptr = m_chr_end = m_iter.get_pointer();
    m_next = m_iter.next();
}

wcwidth_iter::wcwidth_iter(const wcwidth_iter& i)
: m_iter(i.m_iter)
, m_next(i.m_next)
, m_chr_ptr(i.m_chr_ptr)
, m_chr_end(i.m_chr_end)
, m_chr_wcwidth(i.m_chr_wcwidth)
, m_emoji(i.m_emoji)
{
}

// This collects a char run according to the following rules:
//
//  - NUL ends a run without being part of the run.
//  - A control character or DEL is a run by itself.
//  - An emoji codepoint starts a run that includes the codepoint and
//    following codepoints for certain variant selectors, or zero width joiner
//    followed by another emoji codepoint.
//  - Otherwise a run includes a Unicode codepoint and any following
//    codepoints whose wcwidth is 0.
//
// This returns the first codepoint in the run.
char32_t wcwidth_iter::next()
{
    m_chr_ptr = m_chr_end;
    m_emoji = false;

    const char32_t c = m_next;

    if (!c)
    {
        m_chr_wcwidth = 0;
        return c;
    }

    m_chr_end = m_iter.get_pointer();
    m_next = m_iter.next();

    // In the Windows console subsystem, combining marks may have a column
    // width of 0 or 1, depending on the OS version and what codepoints
    // precede the combining mark (esp. when a combining mark or variant
    // selector is the first codepoint in a terminal row).
    combining_mark_width_scope cmwidth(combining_mark_width_scope::mode_normal);

    m_chr_wcwidth = wcwidth(c);
    if (m_chr_wcwidth < 0)
        return c;

    // Try to parse emoji sequences.
    if (g_color_emoji && m_chr_wcwidth)
    {
        // Check for a country flag sequence.
        if (c >= 0x1f1e6 && c <= 0x1f1ff && m_next >= 0x1f1e6 && m_next <= 0x1f1ff)
        {
            m_emoji = true;
            m_chr_wcwidth = 2;
            m_chr_end = m_iter.get_pointer();
            m_next = m_iter.next();
            return c;
        }

        // If it's an emoji character, then try to parse an emoji sequence.
        const bool unq = is_possible_unqualified_half_width(c);
        if (unq || is_emoji(c))
        {
            // A variant selector after an unqualified form makes it
            // fully-qualified and be full width (2 cells).
            if (unq && is_variant_selector(m_next))
            {
                m_chr_end = m_iter.get_pointer();
                m_next = m_iter.next();
fully_qualified:
                assert(m_chr_wcwidth == 1 || m_chr_wcwidth == 2);
                m_chr_wcwidth = std::max<uint16_t>(m_chr_wcwidth, 2);
            }
            else if (c == 0x3030 || c == 0x303d || c == 0x3297 || c == 0x3299)
            {
                // Special cases:  Windows Terminal renders some unqualified
                // emoji the same as their fully-qualified forms.
                assert(m_chr_wcwidth > 0);
                goto fully_qualified;
            }

            // Consume the emoji sequence.
emoji_sequence:
            consume_emoji_sequence();
            m_emoji = true;
            return c;
        }
        else if (is_variant_selector(c))
        {
            assert(m_chr_wcwidth == 1 || m_chr_wcwidth == 2);
            m_chr_wcwidth = std::max<uint16_t>(m_chr_wcwidth, 2);
            goto emoji_sequence;
        }
    }

    // Collect a run until the next non-zero width character.
    while (m_next)
    {
        const int32_t w = wcwidth(m_next);
        if (w != 0)
        {
            // Variant selectors affect non-emoji as well, so treat them as
            // zero width for continuation purposes, but make the width 2.
            if (g_color_emoji && is_variant_selector(m_next))
            {
                assert(m_chr_wcwidth == 1 || m_chr_wcwidth == 2);
                m_chr_wcwidth = std::max<uint16_t>(m_chr_wcwidth, 2);
                m_emoji = true; // These essentially make it an emoji, even if the base character isn't an emoji.
            }
            else
                break;
        }
        m_chr_end = m_iter.get_pointer();
        m_next = m_iter.next();
    }

    return c;
}

void wcwidth_iter::consume_emoji_sequence()
{
    // Within emoji sequences, combining marks always have zero width.
    combining_mark_width_scope cmwidth(combining_mark_width_scope::mode_emoji);

    while (m_next)
    {
        if (is_variant_selector(m_next))
        {
            m_chr_end = m_iter.get_pointer();
            m_next = m_iter.next();
            // Variant selector implies full width emoji (2 cells).
            assert(m_chr_wcwidth >= 0 && m_chr_wcwidth <= 2);
            m_chr_wcwidth = std::max<uint16_t>(m_chr_wcwidth, 2);
        }
        else if (m_next == 0x200d)
        {
            m_chr_end = m_iter.get_pointer();
            m_next = m_iter.next();
            // ZWJ implies full width emoji (2 cells).
            assert(m_chr_wcwidth == 1 || m_chr_wcwidth == 2);
            m_chr_wcwidth = std::max<uint16_t>(m_chr_wcwidth, 2);
            // Stop parsing if the next character is not an emoji.
            if (!is_emoji(m_next) &&
                !is_possible_unqualified_half_width(m_next) &&
                m_next != 0x2640 &&                     // woman
                m_next != 0x2642)                       // man
                break;
            // Accept the next emoji, and advance to continue with the next
            // character, to handle joiners and variants.
            m_chr_end = m_iter.get_pointer();
            m_next = m_iter.next();
        }
        else
            break;
    }
}

void wcwidth_iter::unnext()
{
    assert(m_iter.get_pointer() > m_chr_ptr);
    reset_pointer(m_chr_ptr);
}

const char* wcwidth_iter::get_pointer() const
{
    return m_chr_end;
}

void wcwidth_iter::reset_pointer(const char* s)
{
    m_iter.reset_pointer(s);
    m_chr_end = m_chr_ptr = s;
    m_chr_wcwidth = 0;
    m_emoji = false;
    m_next = m_iter.next();
}

bool wcwidth_iter::more() const
{
    return (m_chr_end < m_iter.get_pointer()) || m_iter.more();
}

size_t wcwidth_iter::length() const
{
    return m_iter.length() + size_t(m_iter.get_pointer() - m_chr_end);
}
