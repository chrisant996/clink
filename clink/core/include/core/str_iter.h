// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"

//------------------------------------------------------------------------------
template <typename T>
class str_iter_impl
{
public:
                str_iter_impl(const T* s, int len=-1);
                str_iter_impl(const str_impl<T>& s, int len=-1);
    const T*    get_pointer() const;
    int         peek();
    int         next();

private:
    const T*    m_ptr;
    const T*    m_end;
};

//------------------------------------------------------------------------------
template <typename T> str_iter_impl<T>::str_iter_impl(const T* s, int len)
: m_ptr(s)
, m_end(m_ptr + len)
{
}

//------------------------------------------------------------------------------
template <typename T> str_iter_impl<T>::str_iter_impl(const str_impl<T>& s, int len)
: m_ptr(s.c_str())
, m_end(m_ptr + len)
{
}

//------------------------------------------------------------------------------
template <typename T> const T* str_iter_impl<T>::get_pointer() const
{
    return m_ptr;
};

//------------------------------------------------------------------------------
template <typename T> int str_iter_impl<T>::peek()
{
    const char* ptr = m_ptr;
    int ret = next();
    m_ptr = ptr;
    return ret;
}

//------------------------------------------------------------------------------
typedef str_iter_impl<char>     str_iter;
typedef str_iter_impl<wchar_t>  wstr_iter;
