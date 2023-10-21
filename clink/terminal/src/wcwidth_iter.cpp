// Copyright (c) 2023 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "wcwidth.h"

#include <assert.h>

//------------------------------------------------------------------------------
extern "C" uint32 clink_wcswidth(const char* s, uint32 len)
{
    uint32 count = 0;

    wcwidth_iter inner_iter(s, len);
    while (inner_iter.next())
        count += inner_iter.character_wcwidth_onectrl();

    return count;
}

//------------------------------------------------------------------------------
extern "C" uint32 clink_wcswidth_expandctrl(const char* s, uint32 len)
{
    uint32 count = 0;

    wcwidth_iter inner_iter(s, len);
    while (inner_iter.next())
        count += inner_iter.character_wcwidth_twoctrl();

    return count;
}



//------------------------------------------------------------------------------
wcwidth_iter::wcwidth_iter(const char* s, int32 len)
: m_iter(s, len)
{
    m_chr_ptr = m_chr_end = m_iter.get_pointer();
    m_next = m_iter.next();
}

//------------------------------------------------------------------------------
wcwidth_iter::wcwidth_iter(const str_impl<char>& s, int32 len)
: m_iter(s, len)
{
    m_chr_ptr = m_chr_end = m_iter.get_pointer();
    m_next = m_iter.next();
}

//------------------------------------------------------------------------------
wcwidth_iter::wcwidth_iter(const wcwidth_iter& i)
: m_iter(i.m_iter)
, m_next(i.m_next)
, m_chr_ptr(i.m_chr_ptr)
, m_chr_end(i.m_chr_end)
, m_chr_wcwidth(i.m_chr_wcwidth)
{
}

//------------------------------------------------------------------------------
// This collects a char run according to the following rules:
//
//  - NUL ends a run without being part of the run.
//  - A control character or DEL is a run by itself.
//  - Certain fully qualified double width color emoji may compose a run with
//    two codepoints where the second is 0xFE0F.
//  - Otherwise a run includes a Unicode codepoint and any following
//    codepoints whose wcwidth is 0.
//
// This returns the first codepoint in the run.
char32_t wcwidth_iter::next()
{
    m_chr_ptr = m_chr_end;

    const char32_t c = m_next;

    if (!c)
    {
        m_chr_wcwidth = 0;
        return c;
    }

    m_chr_end = m_iter.get_pointer();
    m_next = m_iter.next();

    m_chr_wcwidth = wcwidth(c);
    if (m_chr_wcwidth < 0)
        return c;

    extern bool g_color_emoji;
    if (m_next == 0xfe0f &&
        g_color_emoji &&
        m_chr_wcwidth > 0 &&
        is_fully_qualified_double_width_prefix(c))
    {
        m_chr_end = m_iter.get_pointer();
        m_next = m_iter.next();
        ++m_chr_wcwidth;
        assert(m_chr_wcwidth == 2);
        return c;
    }

    while (m_next)
    {
        const int32 w = wcwidth(m_next);
        if (w != 0)
            break;
        m_chr_end = m_iter.get_pointer();
        m_next = m_iter.next();
    }

    return c;
}

//------------------------------------------------------------------------------
void wcwidth_iter::unnext()
{
    assert(m_iter.get_pointer() > m_chr_ptr);
    reset_pointer(m_chr_ptr);
}

//------------------------------------------------------------------------------
const char* wcwidth_iter::get_pointer() const
{
    return m_chr_end;
}

//------------------------------------------------------------------------------
void wcwidth_iter::reset_pointer(const char* s)
{
    m_iter.reset_pointer(s);
    m_chr_end = m_chr_ptr = s;
    m_chr_wcwidth = 0;
    m_next = m_iter.next();
}

//------------------------------------------------------------------------------
bool wcwidth_iter::more() const
{
    return (m_chr_end < m_iter.get_pointer()) || m_iter.more();
}

//------------------------------------------------------------------------------
uint32 wcwidth_iter::length() const
{
    return m_iter.length() + uint32(m_iter.get_pointer() - m_chr_end);
}
