// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class linear_allocator
{
public:
                            linear_allocator(uint32 size);
                            linear_allocator(linear_allocator&& o) = delete;
                            ~linear_allocator();
    linear_allocator&       operator = (linear_allocator&& o);
    void                    reset();
    void                    clear();
    void*                   alloc(uint32 size);
    const char*             store(const char* str);
    template <class T> T*   calloc(uint32 count=1);
    bool                    fits(uint32) const;
    bool                    oversized(uint32) const;

    bool                    unittest_at_end(void* ptr, uint32 size) const;
    bool                    unittest_in_prev_page(void* ptr, uint32 size) const;

private:
    bool                    new_page();
    void                    free_chain(bool keep_one=false);
    char*                   m_ptr = nullptr;
    uint32                  m_used;
    uint32                  m_max;
};

//------------------------------------------------------------------------------
inline void linear_allocator::reset()
{
    free_chain(true/*keep_one*/);
}

//------------------------------------------------------------------------------
inline void linear_allocator::clear()
{
    free_chain(false/*keep_one*/);
}

//------------------------------------------------------------------------------
template <class T> T* linear_allocator::calloc(uint32 count)
{
    void* p = alloc(sizeof(T) * count);
    if (p)
        memset(p, 0, sizeof(T) * count);
    return static_cast<T*>(p);
}

//------------------------------------------------------------------------------
inline bool linear_allocator::fits(uint32 size) const
{
    return m_used + size <= m_max;
}

//------------------------------------------------------------------------------
inline bool linear_allocator::oversized(uint32 size) const
{
    return size + sizeof(m_ptr) > m_max;
}



//------------------------------------------------------------------------------
inline bool linear_allocator::unittest_at_end(void* ptr, uint32 size) const
{
    return ptr == m_ptr + m_used - size;
}

//------------------------------------------------------------------------------
inline bool linear_allocator::unittest_in_prev_page(void* _ptr, uint32 size) const
{
    char* ptr = (char*)_ptr;
    char* prev_page = *reinterpret_cast<char**>(m_ptr);
    if (oversized(size))
        return ptr == prev_page + sizeof(m_ptr);
    return ptr >= prev_page + sizeof(m_ptr) && ptr + size <= prev_page + m_max;
}
