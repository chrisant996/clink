// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str_iter.h"

//------------------------------------------------------------------------------
const char*     str_iter_impl<char>::s_empty    = "";
const wchar_t*  str_iter_impl<wchar_t>::s_empty = L"";

//------------------------------------------------------------------------------
template <>
int str_iter_impl<char>::next()
{
    if (!more())
        return 0;

    int ax = 0;
    int encode_length = 0;
    while (int c = *m_ptr++)
    {
        ax = (ax << 6) | (c & 0x7f);
        if (encode_length)
        {
            --encode_length;
            continue;
        }

        if ((c & 0xc0) < 0xc0)
            return ax;

        if (encode_length = !!(c & 0x20))
            encode_length += !!(c & 0x10);

        ax &= (0x1f >> encode_length);

        if (!more())
            break;
    }

    return 0;
}

//------------------------------------------------------------------------------
template <>
int str_iter_impl<wchar_t>::next()
{
    if (!more())
        return 0;

    int ax = 0;
    while (int c = *m_ptr++)
    {
        // Decode surrogate pairs.
        if ((c & 0xfc00) == 0xd800)
        {
            ax = c << 10;
            continue;
        }
        else if ((c & 0xfc00) == 0xdc00 && ax >= (1 << 10))
            return ax + c - 0x35fdc00;
        else
            return c;
    }

    return 0;
}

//------------------------------------------------------------------------------
template <>
unsigned int str_iter_impl<char>::length() const
{
    return (unsigned int)((m_ptr <= m_end) ? m_end - m_ptr : strlen(m_ptr));
}

//------------------------------------------------------------------------------
template <>
unsigned int str_iter_impl<wchar_t>::length() const
{
    return (unsigned int)((m_ptr <= m_end) ? m_end - m_ptr : wcslen(m_ptr));
}
