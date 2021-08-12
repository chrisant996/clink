// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"

//------------------------------------------------------------------------------
template <typename T>
class str_iter_impl
{
public:
    explicit        str_iter_impl(const T* s=(const T*)L"", int len=-1);
    explicit        str_iter_impl(const str_impl<T>& s, int len=-1);
                    str_iter_impl(const str_iter_impl<T>& i);
    const T*        get_pointer() const;
    const T*        get_next_pointer();
    void            reset_pointer(const T* ptr);
    int             peek();
    int             next();
    bool            more() const;
    unsigned int    length() const;

private:
    const T*        m_ptr;
    const T*        m_end;
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
template <typename T> str_iter_impl<T>::str_iter_impl(const str_iter_impl<T>& i)
: m_ptr(i.m_ptr)
, m_end(i.m_end)
{
}

//------------------------------------------------------------------------------
template <typename T> const T* str_iter_impl<T>::get_pointer() const
{
    return m_ptr;
};

//------------------------------------------------------------------------------
template <typename T> const T* str_iter_impl<T>::get_next_pointer()
{
    const T* ptr = m_ptr;
    next();
    const T* ret = m_ptr;
    m_ptr = ptr;
    return ret;
};

//------------------------------------------------------------------------------
template <typename T> void str_iter_impl<T>::reset_pointer(const T* ptr)
{
    assert(ptr);
    assert(ptr <= m_ptr);
    m_ptr = ptr;
}

//------------------------------------------------------------------------------
template <typename T> int str_iter_impl<T>::peek()
{
    const T* ptr = m_ptr;
    int ret = next();
    m_ptr = ptr;
    return ret;
}

//------------------------------------------------------------------------------
template <typename T> bool str_iter_impl<T>::more() const
{
    return (m_ptr != m_end && *m_ptr != '\0');
}



//------------------------------------------------------------------------------
typedef str_iter_impl<char>     str_iter;
typedef str_iter_impl<wchar_t>  wstr_iter;
