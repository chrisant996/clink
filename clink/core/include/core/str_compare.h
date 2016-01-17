// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "base.h"
#include "str_iter.h"

#include <Windows.h>

//------------------------------------------------------------------------------
class str_compare_scope
{
public:
    enum
    {
        exact,
        caseless,
        relaxed,    // case insensitive with -/_ consider equivalent.
    };

                str_compare_scope(int mode);
                ~str_compare_scope();
    static int  current();

private:
    int         m_prev_mode;
    THREAD_LOCAL static int ts_mode;
};



//------------------------------------------------------------------------------
template <class T>
int str_compare(const T* lhs, const T* rhs)
{
    return str_compare(str_iter_impl<T>(lhs), str_iter_impl<T>(rhs));
}

//------------------------------------------------------------------------------
template <class T>
int str_compare(const str_impl<T>& lhs, const str_impl<T>& rhs)
{
    return str_compare(str_iter_impl<T>(lhs), str_iter_impl<T>(rhs));
}

//------------------------------------------------------------------------------
template <class T>
int str_compare(str_iter_impl<T>& lhs, str_iter_impl<T>& rhs)
{
    switch (str_compare_scope::current())
    {
    case str_compare_scope::relaxed:  return str_compare_impl<T, 2>(lhs, rhs);
    case str_compare_scope::caseless: return str_compare_impl<T, 1>(lhs, rhs);
    default:                          return str_compare_impl<T, 0>(lhs, rhs);
    }
}

//------------------------------------------------------------------------------
template <class T, int MODE>
int str_compare_impl(str_iter_impl<T>& lhs, str_iter_impl<T>& rhs)
{
    int diff_index = 0;
    while (int c = lhs.next())
    {
        int d = rhs.next();

        if (MODE > 0)
        {
            c = (c > 0xffff) ? c : int(CharLowerW(LPWSTR(c)));
            d = (d > 0xffff) ? d : int(CharLowerW(LPWSTR(d)));
        }

        if (MODE > 1)
        {
            c = (c == '-') ? '_' : c;
            d = (d == '-') ? '_' : d;
        }

        if (c != d)
            return diff_index;

        ++diff_index;
    }

    return !rhs.next() ? -1 : diff_index;
}
