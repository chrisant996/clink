// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str_tokeniser.h"

//------------------------------------------------------------------------------
template <typename T>
bool next_impl(str_iter_impl<T>& iter, const T*& out_start, int& out_length, const char* delims)
{
    // Skip initial delimiters.
    while (int c = iter.peek())
    {
        if (strchr(delims, c) == nullptr)
            break;

        iter.next();
    }

    // Extract the delimited string.
    const T* start = iter.get_pointer();

    while (int c = iter.peek())
    {
        if (strchr(delims, c))
            break;

        iter.next();
    }

    const T* end = iter.get_pointer();
    iter.next();

    // Empty string? Must be the end of the input. We're done here.
    if (start == end)
        return false;

    // Set the output and return.
    out_start = start;
    out_length = int(end - start);
    return true;
}

//------------------------------------------------------------------------------
template <> bool str_tokeniser_impl<char>::next(str_impl<char>& out)
{
    const char* start;
    int length;
    bool ret = next_impl<char>(m_iter, start, length, m_delims);

    out.clear();
    if (ret)
        out.concat(start, length);

    return ret;
}

//------------------------------------------------------------------------------
template <> bool str_tokeniser_impl<wchar_t>::next(str_impl<wchar_t>& out)
{
    const wchar_t* start;
    int length;
    bool ret = next_impl<wchar_t>(m_iter, start, length, m_delims);

    out.clear();
    if (ret)
        out.concat(start, length);

    return ret;
}

//------------------------------------------------------------------------------
template <> bool str_tokeniser_impl<char>::next(const char*& start, int& length)
{
    return next_impl<char>(m_iter, start, length, m_delims);
}

//------------------------------------------------------------------------------
template <> bool str_tokeniser_impl<wchar_t>::next(const wchar_t*& start, int& length)
{
    return next_impl<wchar_t>(m_iter, start, length, m_delims);
}
