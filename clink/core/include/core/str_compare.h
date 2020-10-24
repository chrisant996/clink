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
        relaxed,    // case insensitive with -/_ considered equivalent.
    };

                str_compare_scope(int mode);
                ~str_compare_scope();
    static int  current();

private:
    int         m_prev_mode;
    threadlocal static int ts_mode;
};



//------------------------------------------------------------------------------
template <class T, int MODE>
int str_compare_impl(str_iter_impl<T>& lhs, str_iter_impl<T>& rhs)
{
    const T* start = lhs.get_pointer();

    while (1)
    {
        int c = lhs.peek();
        int d = rhs.peek();
        if (!c || !d)
            break;

        if (MODE > 0)
        {
            c = (c > 0xffff) ? c : int(uintptr_t(CharLowerW(LPWSTR(uintptr_t(c)))));
            d = (d > 0xffff) ? d : int(uintptr_t(CharLowerW(LPWSTR(uintptr_t(d)))));
        }

        if (MODE > 1)
        {
            c = (c == '-') ? '_' : c;
            d = (d == '-') ? '_' : d;
        }

        if (c == '\\') c = '/';
        if (d == '\\') d = '/';

        if (c != d)
            break;

        lhs.next();
        rhs.next();
    }

    if (lhs.more() || rhs.more())
        return int(lhs.get_pointer() - start);

    return -1;
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
template <class T>
int str_compare(const T* lhs, const T* rhs)
{
    str_iter_impl<T> lhs_iter(lhs);
    str_iter_impl<T> rhs_iter(rhs);
    return str_compare(lhs_iter, rhs_iter);
}

//------------------------------------------------------------------------------
template <class T>
int str_compare(const str_impl<T>& lhs, const str_impl<T>& rhs)
{
    str_iter_impl<T> lhs_iter(lhs);
    str_iter_impl<T> rhs_iter(rhs);
    return str_compare(lhs_iter, rhs_iter);
}
