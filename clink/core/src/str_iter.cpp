// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str_iter.h"

//------------------------------------------------------------------------------
template <>
int32 str_iter_impl<char>::next()
{
    if (!more())
        return 0;

    // TODO:  Detect invalid UTF8 correctly.

    int32 ax = 0;
    int32 encode_length = 0;
    while (int32 c = uint8(*m_ptr++))
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
int32 str_iter_impl<wchar_t>::next()
{
    int32 c;
    int32 ax = 0;

    while (more() && (c = *m_ptr++))
    {
        // Decode surrogate pairs.
        if ((c & 0xfc00) == 0xd800)
        {
            if (!more() || (*m_ptr & 0xfc00) != 0xdc00)         // Invalid.
                return 0xfffd;
            ax = c << 10;
            continue;
        }
        else if ((c & 0xfc00) == 0xdc00)
        {
            if (ax < (1 << 10))                                 // Invalid.
                return 0xfffd;
            c = ax + c - 0x35fdc00;
            ax = 0;
        }
        else
        {
            if (ax)                                             // Invalid.
               return 0xfffd;
        }
        return c;
    }

    if (ax)                                                     // Invalid.
        return 0xfffd;
    return 0;
}

//------------------------------------------------------------------------------
template <>
uint32 str_iter_impl<char>::length() const
{
    return (uint32)((m_ptr <= m_end) ? m_end - m_ptr : strlen(m_ptr));
}

//------------------------------------------------------------------------------
template <>
uint32 str_iter_impl<wchar_t>::length() const
{
    return (uint32)((m_ptr <= m_end) ? m_end - m_ptr : wcslen(m_ptr));
}
