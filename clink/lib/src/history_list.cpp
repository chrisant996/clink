// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "history_list.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/log.h>
#include <assert.h>

#include <memory>

#include <core/debugheap.h>

//------------------------------------------------------------------------------
void history_list::clear()
{
    m_index = 0;
    m_entries.clear();
}

//------------------------------------------------------------------------------
bool history_list::add_history(const char* line, const char* timestamp)
{
    if (get_count() >= c_max_capacity)
        return false;

    std::unique_ptr<hist_entry> e = std::make_unique<hist_entry>();
    e->m_line = line;
    if (timestamp)
        e->m_timestamp = timestamp;

    m_entries.emplace_back(std::move(e));
    return true;
}

//------------------------------------------------------------------------------
bool history_list::remove_history(uint32 index)
{
    if (index >= get_count())
        return false;

    if (m_index && m_index >= index)
        --m_index;
    m_entries.erase(m_entries.begin() + index);
    return true;
}

//------------------------------------------------------------------------------
bool history_list::set_index(uint32 index)
{
    if (index > get_count())
        return false;

    m_index = index;
    return true;
}

//------------------------------------------------------------------------------
const hist_entry* history_list::get_entry(uint32 index) const
{
    if (index >= m_entries.size())
        return nullptr;

    return m_entries[index].get();
}
