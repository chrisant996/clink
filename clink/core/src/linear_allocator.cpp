// Copyright (c) 2016 Martin Ridgers
// Portions Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "linear_allocator.h"

#include <stdlib.h>
#include <assert.h>

//------------------------------------------------------------------------------
linear_allocator::linear_allocator(uint32 size)
: m_used(size)
, m_max(size)
{
    assert(size > sizeof(m_ptr)); // Warn since allocations will never succeed.
}

//------------------------------------------------------------------------------
linear_allocator::~linear_allocator()
{
    free_chain();
}

//------------------------------------------------------------------------------
linear_allocator& linear_allocator::operator = (linear_allocator&& o)
{
    free_chain();

    m_ptr = o.m_ptr;
    m_used = o.m_used;
    m_max = o.m_max;

    o.m_ptr = nullptr;
    o.m_used = o.m_max;

    return *this;
}

//------------------------------------------------------------------------------
void* linear_allocator::alloc(uint32 size)
{
    if (size == 0)
        return nullptr;

    if (oversized(size))
    {
        if (!m_ptr && !new_page())
            return nullptr;
        // An over-sized allocation gets its own "page", which gets inserted
        // into the chain without discarding the current page.
        char* oversized = (char*)malloc(size + sizeof(m_ptr));
        if (oversized == nullptr)
            return nullptr;
#ifdef DEBUG
        m_footprint += size + sizeof(m_ptr);
#endif
        *reinterpret_cast<char**>(oversized) = *reinterpret_cast<char**>(m_ptr);
        *reinterpret_cast<char**>(m_ptr) = oversized;
        return oversized + sizeof(m_ptr);
    }

    if (!fits(size) && !new_page())
        return nullptr;

    void* ret = m_ptr + m_used;
    m_used += size;
    return ret;
}

//------------------------------------------------------------------------------
const char* linear_allocator::store(const char* str)
{
    const uint32 size = uint32(str ? strlen(str) + 1 : 1);
    char* ret = (char*)alloc(size);
    if (!ret)
        return nullptr;

    memcpy(ret, str, size);
    return ret;
}

//------------------------------------------------------------------------------
bool linear_allocator::new_page()
{
    if (m_max < sizeof(m_ptr))
        return false;

    char* temp = (char*)malloc(m_max);
    if (temp == nullptr)
        return false;

#ifdef DEBUG
    m_footprint += m_max;
#endif

    *reinterpret_cast<char**>(temp) = m_ptr;
    m_used = sizeof(m_ptr);
    m_ptr = temp;
    return true;
}

//------------------------------------------------------------------------------
void linear_allocator::free_chain(bool keep_one)
{
    m_used = m_ptr && keep_one ? sizeof(m_ptr) : m_max;
#ifdef DEBUG
    m_footprint = m_ptr && keep_one ? m_max : 0;
#endif

    char* ptr = m_ptr;

    if (!keep_one)
        m_ptr = nullptr;

    while (ptr)
    {
        char* tmp = ptr;
        ptr = *reinterpret_cast<char**>(ptr);
        if (keep_one)
        {
            *reinterpret_cast<char**>(tmp) = nullptr;
            tmp = nullptr;
            keep_one = false;
        }
        free(tmp);
    }
}
