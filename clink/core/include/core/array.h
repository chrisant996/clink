// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
template <typename T>
class array
{
public:
    template <int D>
    class iter_impl
    {
    public:
                    iter_impl(T* t) : m_t(t)               {}
        void        operator ++ ()                         { m_t += D; }
        T&          operator * () const                    { return *m_t; }
        T&          operator -> () const                   { return *m_t; }
        bool        operator != (const iter_impl& i) const { return i.m_t != m_t; }

    private:
        T*          m_t;
    };

    typedef iter_impl<1>    iter;
    typedef iter_impl<-1>   riter;

                    array(T* ptr, unsigned int size, unsigned int capacity=0);
    iter            begin() const    { return m_ptr; }
    iter            end() const      { return m_ptr + m_size; }
    riter           rbegin() const   { return m_ptr + m_size - 1; }
    riter           rend() const     { return m_ptr - 1; }
    unsigned int    size() const     { return m_size; }
    unsigned int    capacity() const { return m_size; }
    bool            empty() const    { return !m_size; }
    bool            full() const     { return (m_size == m_capacity); }
    T*              front() const    { return m_ptr; }
    T*              back() const     { return empty() ? nullptr : (m_ptr + m_size - 1); }
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
