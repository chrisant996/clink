// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>

//------------------------------------------------------------------------------
matches::store::store(unsigned int size)
: m_size(size)
, m_front(0)
, m_back(size)
{
    m_ptr = (char*)malloc(size);
}

//------------------------------------------------------------------------------
matches::store::~store()
{
    free(m_ptr);
}

//------------------------------------------------------------------------------
void matches::store::reset()
{
    m_back = m_size;
    m_front = 0;
}

//------------------------------------------------------------------------------
const char* matches::store::get(unsigned int id) const
{
    id <<= 1;
    return (id < m_size) ? (m_ptr + id) : nullptr;
}

//------------------------------------------------------------------------------
int matches::store::store_front(const char* str)
{
    unsigned int size = get_size(str);
    unsigned int next = m_front + size;
    if (next > m_back)
        return -1;

    str_base(m_ptr + m_front, size).copy(str);

    unsigned int ret = m_front;
    m_front = next;
    return ret >> 1;
}

//------------------------------------------------------------------------------
int matches::store::store_back(const char* str)
{
    unsigned int size = get_size(str);
    unsigned int next = m_back + size;
    if (next > m_front)
        return -1;

    str_base(m_ptr + m_back, size).copy(str);

    unsigned int ret = m_back;
    m_back = next;
    return ret >> 1;
}

//------------------------------------------------------------------------------
unsigned int matches::store::get_size(const char* str) const
{
    if (str == nullptr || str[0] == '\0')
        return ~0u;

    int length = int(strlen(str) + 1);
    length = (length + 1) & ~1;
    return length;
}



//------------------------------------------------------------------------------
matches::matches(unsigned int store_size)
: m_store(min(store_size, 0x10000u))
{
    m_infos.reserve(1024);
}

//------------------------------------------------------------------------------
unsigned int matches::get_match_count() const
{
    return (unsigned int)m_infos.size();
}

//------------------------------------------------------------------------------
const char* matches::get_match(unsigned int index) const
{
    if (index >= (unsigned int)m_infos.size())
        return nullptr;

    unsigned int store_id = m_infos[index].store_id;
    return m_store.get(store_id);
}

//------------------------------------------------------------------------------
void matches::get_match_lcd(str_base& out) const
{
    int match_count = get_match_count();

    if (match_count <= 0)
        return;

    if (match_count == 1)
    {
        out = get_match(0);
        return;
    }

    out = get_match(0);
    int lcd_length = out.length();

    str_compare_scope _(str_compare_scope::caseless);

    for (int i = 1, n = get_match_count(); i < n; ++i)
    {
        const char* match = get_match(i);
        int d = str_compare(match, out.c_str());
        if (d >= 0)
            lcd_length = min(d, lcd_length);
    }

    out.truncate(lcd_length);
}

//------------------------------------------------------------------------------
void matches::reset()
{
    m_infos.clear();
    m_store.reset();
}

//------------------------------------------------------------------------------
void matches::add_match(const char* match)
{
    int store_id = m_store.store_front(match);
    if (store_id < 0)
        return;

    m_infos.push_back({ (unsigned short)store_id });
}
