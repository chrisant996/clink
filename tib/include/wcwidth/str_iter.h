// Copyright (c) 2026 Christopher Antos
// Portions Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

template <typename T>
class str_iter_impl
{
public:
    explicit        str_iter_impl(const T* s=(const T*)L"", size_t len=-1);
                    str_iter_impl(const str_iter_impl<T>& i);
    const T*        get_pointer() const;
    const T*        get_next_pointer();
    void            reset_pointer(const T* ptr);
    void            truncate(size_t len);
    char32_t        peek();
    char32_t        next();
    bool            more() const;
    size_t          length() const;

private:
    const T*        m_ptr;
    const T*        m_end;
};

template <typename T> str_iter_impl<T>::str_iter_impl(const T* s, size_t len)
: m_ptr(s)
, m_end(m_ptr + len)
{
}

template <typename T> str_iter_impl<T>::str_iter_impl(const str_iter_impl<T>& i)
: m_ptr(i.m_ptr)
, m_end(i.m_end)
{
}

template <typename T> const T* str_iter_impl<T>::get_pointer() const
{
    return m_ptr;
};

template <typename T> const T* str_iter_impl<T>::get_next_pointer()
{
    const T* ptr = m_ptr;
    next();
    const T* ret = m_ptr;
    m_ptr = ptr;
    return ret;
};

template <typename T> void str_iter_impl<T>::reset_pointer(const T* ptr)
{
    assert(ptr);
    assert(ptr <= m_ptr);
    m_ptr = ptr;
}

template <typename T> void str_iter_impl<T>::truncate(size_t len)
{
    assert(m_ptr);
    assert(len <= length());
    m_end = m_ptr + len;
}

template <typename T> char32_t str_iter_impl<T>::peek()
{
    const T* ptr = m_ptr;
    char32_t ret = next();
    m_ptr = ptr;
    return ret;
}

template <typename T> bool str_iter_impl<T>::more() const
{
    return (m_ptr != m_end && *m_ptr != '\0');
}

typedef str_iter_impl<char> str_iter;
#ifdef _WIN32
typedef str_iter_impl<WCHAR> wstr_iter;
#endif
