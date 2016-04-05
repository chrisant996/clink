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

    while (int c = m_iter.peek())
    {
        if (strchr(m_delims, c))
            break;

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

//------------------------------------------------------------------------------
template <typename T>
void str_tokeniser_impl<T>::dequote(str_impl<T>& out) const
{
}

//------------------------------------------------------------------------------
template <typename T>
void str_tokeniser_impl<T>::dequote(const T*& start, int& length) const
{
}
