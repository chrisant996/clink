// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
template <typename T>
class array
{
    /* This class is really rather poor */

public:
    template <int D, typename U>
    class iter_impl
    {
    public:
                    iter_impl(U* u) : m_u(u)               {}
        void        operator ++ ()                         { m_u += D; }
        U&          operator * () const                    { return *m_u; }
        U&          operator -> () const                   { return *m_u; }
        bool        operator != (const iter_impl& i) const { return i.m_u != m_u; }

    private:
        U*          m_u;
    };

    typedef iter_impl<1, T>         iter;
    typedef iter_impl<1, T const>   citer;
    typedef iter_impl<-1, T>        riter;
    typedef iter_impl<-1, T const>  rciter;

                    array(T* ptr, unsigned int size, unsigned int capacity=0);
    iter            begin()          { return m_ptr; }
    iter            end()            { return m_ptr + m_size; }
    citer           begin() const    { return m_ptr; }
    citer           end() const      { return m_ptr + m_size; }
    riter           rbegin()         { return m_ptr + m_size - 1; }
    riter           rend()           { return m_ptr - 1; }
    rciter          rbegin() const   { return m_ptr + m_size - 1; }
    rciter          rend() const     { return m_ptr - 1; }
    unsigned int    size() const     { return m_size; }
    unsigned int    capacity() const { return m_capacity; }
    bool            empty() const    { return !m_size; }
    bool            full() const     { return (m_size == m_capacity); }
    T const*        front() const    { return m_ptr; }
    T*              front()          { return m_ptr; }
    T const*        back() const     { return empty() ? nullptr : (m_ptr + m_size - 1); }
    T*              back()           { return empty() ? nullptr : (m_ptr + m_size - 1); }
    T*              push_back()      { return full() ? nullptr : (m_ptr + m_size++); }
    void            clear();
    T const*        operator [] (unsigned int index) const;

protected:
    T*              m_ptr;
    unsigned int    m_size;
    unsigned int    m_capacity;
};

//------------------------------------------------------------------------------
template <typename T>
array<T>::array(T* ptr, unsigned int size, unsigned int capacity)
: m_ptr(ptr)
, m_size(size)
, m_capacity(capacity ? capacity : size)
{
}

//------------------------------------------------------------------------------
template <typename T>
T const* array<T>::operator [] (unsigned int index) const
{
    return (index >= capacity()) ? nullptr : (m_ptr + index);
}

//------------------------------------------------------------------------------
template <typename T>
void array<T>::clear()
{
    for (auto iter : *this)
        iter.~T();

    m_size = 0;
}



//------------------------------------------------------------------------------
template <typename T, unsigned int SIZE>
class fixed_array
    : public array<T>
{
public:
                fixed_array() : array<T>(m_buffer, 0, SIZE) {}

private:
    T           m_buffer[SIZE];
};
