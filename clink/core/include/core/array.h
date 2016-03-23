// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
template <typename T>
class array
{
public:
                array(T* ptr, int size, int capacity);
    T*          begin() const    { return m_ptr; }
    T*          end() const      { return m_ptr + m_size; }
    int         size() const     { return m_size; }
    int         capacity() const { return m_size; }
    bool        empty() const    { return !m_size; }
    bool        full() const     { return (m_size == m_capacity); }
    //const T&    operator [] (int index) const;

protected:
    T*          m_ptr;
    int         m_size;
    int         m_capacity;
};

//------------------------------------------------------------------------------
template <typename T>
array<T>::array(T* ptr, int size, int capacity)
: m_ptr(ptr)
, m_size(size)
, m_capacity(capacity)
{
}



//------------------------------------------------------------------------------
template <typename T, int SIZE>
class fixed_array
    : public array<T>
{
public:
                fixed_array();
    T*          push_back();
    T*          back();
    T*          operator [] (int index);

private:
    T           m_buffer[SIZE];
};

//------------------------------------------------------------------------------
template <typename T, int SIZE>
fixed_array<T, SIZE>::fixed_array()
: array(m_buffer, 0, SIZE)
{
}

//------------------------------------------------------------------------------
template <typename T, int SIZE>
T* fixed_array<T, SIZE>::push_back()
{
    if (full())
        return nullptr;

    return m_buffer + m_size++;
}

//------------------------------------------------------------------------------
template <typename T, int SIZE>
T* fixed_array<T, SIZE>::back()
{
    if (empty())
        return nullptr;

    return m_buffer + m_size - 1;
}

//------------------------------------------------------------------------------
template <typename T, int SIZE>
T* fixed_array<T, SIZE>::operator [] (int index)
{
    if (index >= capacity())
        return nullptr;

    return m_buffer + index;
}
