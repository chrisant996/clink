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

extern "C" {
#include <readline/rldefs.h>    // Needed for HAVE_STRING in history.h.
#include <readline/history.h>
#include <readline/histlib.h>   // Depends on config.h.
}

//------------------------------------------------------------------------------
void history_list::clear()
{
    m_index = 0;
    m_entries.clear();
    clear_saved_entry();
}

//------------------------------------------------------------------------------
bool history_list::add_history(const char* line, const char* timestamp)
{
    if (length() >= c_max_capacity)
        return false;

    std::unique_ptr<hist_entry> e = std::make_unique<hist_entry>();
    e->m_line = line;
    if (timestamp && *timestamp)
        e->m_timestamp = timestamp;

    m_entries.emplace_back(std::move(e));
    return true;
}

//------------------------------------------------------------------------------
bool history_list::remove_history(int32 index)
{
    if (index < 0 || index >= length())
        return false;

    if (m_index && m_index >= index)
        --m_index;
    m_entries.erase(m_entries.begin() + index);
    return true;
}

//------------------------------------------------------------------------------
bool history_list::set_index(int32 index)
{
    if (index < 0 || index > length())
        return false;

    m_index = index;
    return true;
}

//------------------------------------------------------------------------------
const hist_entry* history_list::get_entry(int32 index) const
{
    if (index < 0 || index >= m_entries.size())
        return nullptr;

    return m_entries[index].get();
}

//------------------------------------------------------------------------------
const char* history_list::get_line(int32 index) const
{
    if (index < 0 || index >= m_entries.size())
        return nullptr;

    return m_entries[index]->m_line.get();
}

//------------------------------------------------------------------------------
void history_list::clear_saved_entry()
{
    m_saved.reset();
}

//------------------------------------------------------------------------------
bool history_list::save_entry(const char* line)
{
    assert(!m_saved);
    if (m_saved)
        return false;

    m_saved = std::make_unique<hist_entry>();
    m_saved->m_line = line;
    return true;
}

//------------------------------------------------------------------------------
const hist_entry* history_list::get_saved_entry() const
{
    return m_saved.get();
}

//------------------------------------------------------------------------------
void clear_history()
{
    history_list* list = history_list::get();

    list->clear();

    history_prev_use_curr = 0;

    free(const_cast<char*>(history_event_lookup_cache.search_string));
    memset(&history_event_lookup_cache, 0, sizeof(history_event_lookup_cache));
}
