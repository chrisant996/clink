// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>

//------------------------------------------------------------------------------
const char* match_store::get(unsigned int id) const
{
    id <<= 1;
    return (id < m_size) ? (m_ptr + id) : nullptr;
}



//------------------------------------------------------------------------------
matches::store_impl::store_impl(unsigned int size)
: m_front(0)
, m_back(size)
{
    m_size = size;
    m_ptr = (char*)malloc(size);
}

//------------------------------------------------------------------------------
matches::store_impl::~store_impl()
{
    free(m_ptr);
}

//------------------------------------------------------------------------------
void matches::store_impl::reset()
{
    m_back = m_size;
    m_front = 0;
}

//------------------------------------------------------------------------------
int matches::store_impl::store_front(const char* str)
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
int matches::store_impl::store_back(const char* str)
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
unsigned int matches::store_impl::get_size(const char* str) const
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
unsigned int matches::get_info_count() const
{
    return int(m_infos.size());
}

//------------------------------------------------------------------------------
match_info* matches::get_infos()
{
    return &(m_infos[0]);
}

//------------------------------------------------------------------------------
const match_store& matches::get_store() const
{
    return m_store;
}

//------------------------------------------------------------------------------
unsigned int matches::get_match_count() const
{
    return m_count;
}

//------------------------------------------------------------------------------
const char* matches::get_match(unsigned int index) const
{
    if (index >= get_match_count())
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
    m_store.reset();
    m_infos.clear();
    m_coalesced = false;
    m_count = 0;
}

//------------------------------------------------------------------------------
void matches::add_match(const char* match)
{
    if (m_coalesced || !*match)
        return;

    int store_id = m_store.store_front(match);
    if (store_id < 0)
        return;

    m_infos.push_back({ 0, (unsigned short)store_id });
    ++m_count;
}

//------------------------------------------------------------------------------
void matches::coalesce(unsigned int count_hint)
{
    match_info* infos = &(m_infos[0]);

    unsigned int j = 0;
    for (int i = 0, n = int(m_infos.size()); i < n && j < count_hint; ++i)
    {
        if (!infos[i].selected)
            continue;

        if (i != j)
        {
            match_info temp = infos[j];
            infos[j] = infos[i];
            infos[i] = temp;
        }
        ++j;
    }

    m_count = j;
    m_coalesced = true;
}
