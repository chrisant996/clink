// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
template <typename T>
class array
{
public:
                    array(T* ptr, unsigned int size, unsigned int capacity);
    T*              begin() const    { return m_ptr; }
    T*              end() const      { return m_ptr + m_size; }
    unsigned int    size() const     { return m_size; }
    unsigned int    capacity() const { return m_size; }
    bool            empty() const    { return !m_size; }
    bool            full() const     { return (m_size == m_capacity); }
    T*              back() const;
    const T*        operator [] (unsigned int index) const;

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
, m_capacity(capacity)
{
}

//------------------------------------------------------------------------------
template <typename T>
T* array<T>::back() const
{
    if (empty())
        return nullptr;

    return m_ptr + m_size - 1;
}

//------------------------------------------------------------------------------
template <typename T>
const T* array<T>::operator [] (unsigned int index) const
{
    if (index >= capacity())
        return nullptr;

    return m_ptr + index;
}



//------------------------------------------------------------------------------
template <typename T, unsigned int SIZE>
class fixed_array
    : public array<T>
{
public:
                fixed_array();
    T*          push_back();

private:
    T           m_buffer[SIZE];
};

//------------------------------------------------------------------------------
template <typename T, unsigned int SIZE>
fixed_array<T, SIZE>::fixed_array()
: array(m_buffer, 0, SIZE)
{
}

//------------------------------------------------------------------------------
template <typename T, unsigned int SIZE>
T* fixed_array<T, SIZE>::push_back()
{
    if (full())
        return nullptr;

    return m_buffer + m_size++;
}
