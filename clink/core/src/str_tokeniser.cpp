// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str_tokeniser.h"

//------------------------------------------------------------------------------
template <>
bool str_tokeniser_impl<char>::next(str_impl<char>& out)
{
    const char* start;
    int length;
    bool ret = next_impl(start, length);

    out.clear();
    if (ret)
        out.concat(start, length);

    return ret;
}

//------------------------------------------------------------------------------
template <>
bool str_tokeniser_impl<wchar_t>::next(str_impl<wchar_t>& out)
{
    const wchar_t* start;
    int length;
    bool ret = next_impl(start, length);

    out.clear();
    if (ret)
        out.concat(start, length);

    return ret;
}

//------------------------------------------------------------------------------
template <>
bool str_tokeniser_impl<char>::next(const char*& start, int& length)
{
    return next_impl(start, length);
}

//------------------------------------------------------------------------------
template <>
bool str_tokeniser_impl<wchar_t>::next(const wchar_t*& start, int& length)
{
    return next_impl(start, length);
}

//------------------------------------------------------------------------------
template <typename T>
int str_tokeniser_impl<T>::get_right_quote(int left) const
{
    for (const quote& iter : m_quotes)
        if (iter.left == left)
            return iter.right;

    return 0;
}

//------------------------------------------------------------------------------
template <typename T>
bool str_tokeniser_impl<T>::next_impl(const T*& out_start, int& out_length)
{
    // Skip initial delimiters.
    while (int c = m_iter.peek())
    {
        if (strchr(m_delims, c) == nullptr)
            break;

        m_iter.next();
    }

    // Extract the delimited string.
    const T* start = m_iter.get_pointer();

    int quote_close = 0;
    while (int c = m_iter.peek())
    {
        if (quote_close)
        {
            quote_close = (quote_close == c) ? 0 : quote_close;
            m_iter.next();
            continue;
        }

        if (strchr(m_delims, c))
            break;

        quote_close = get_right_quote(c);
        m_iter.next();
    }

    const T* end = m_iter.get_pointer();
    m_iter.next();

    // Empty string? Must be the end of the input. We're done here.
    if (start == end)
        return false;

    // Set the output and return.
    out_start = start;
    out_length = int(end - start);
    return true;
}
