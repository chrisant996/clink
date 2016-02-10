// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str_tokeniser.h"

//------------------------------------------------------------------------------
template <typename T>
bool next_impl(str_iter_impl<T>& iter, str_impl<T>& out, const char* delims)
{
    const T* start = iter.get_pointer();

    while (int c = iter.next())
        if (strchr(delims, c))
            break;

    const T* end = iter.get_pointer();
    if (start == end)
        return false;

    out.clear();
    out.concat(start, int(end - start));
    return true;
}

//------------------------------------------------------------------------------
template <> bool str_tokeniser_impl<char>::next(str_impl<char>& out)
{
    return next_impl<char>(m_iter, out, m_delims);
}

//------------------------------------------------------------------------------
template <> bool str_tokeniser_impl<wchar_t>::next(str_impl<wchar_t>& out)
{
    return next_impl<wchar_t>(m_iter, out, m_delims);
}
