// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "history_list.h"
#include "rl_integration.h"
#include "suggestions.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_compare.h>
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
    m_needle.clear();
    m_last_found = nullptr;
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
    m_last_found = nullptr;
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
    m_last_found = nullptr;
    return true;
}

//------------------------------------------------------------------------------
void history_list::reset_cursor()
{
    m_index = length();
    history_prev_use_curr = 0;
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
void history_list::init_search(const char* line, int32 caret)
{
    m_needle.clear();
    m_last_found = nullptr;
    if (!line)
        return;

    assert(caret >= 0);
    const auto len = str_len(line);
    if (caret < 0)
        caret = 0;
    if (uint32(caret) > len)
        caret = len;
    m_needle.concat(line, caret);
}

//------------------------------------------------------------------------------
bool history_list::has_search_string() const
{
    return !m_needle.empty();
}

//------------------------------------------------------------------------------
int32 history_list::search(int32 start, int32 direction, int32& new_caret) const
{
    assert(has_search_string());
    if (!has_search_string())
        return -1;

    // Skip past duplicates.
    const char* line;
    while (true)
    {
        // TODO: check for signaled.

        start += direction;
        line = get_line(start);
        if (!line)
            return -1;

        if (m_last_found && strcmp(line, m_last_found) == 0)
            continue;

        // Compare the search string with the candidate line.
        const auto cmp = str_compare(line, m_needle.c_str());
        if (cmp < 0 || cmp == m_needle.length())
        {
            m_last_found = line;
            return start;
        }
    }
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

//------------------------------------------------------------------------------
static void replace_from_history(const hist_entry* entry)
{
    history_list* unsave_list = nullptr;
    if (!entry)
    {
        unsave_list = history_list::get();
        entry = unsave_list->get_saved_entry();
    }
    assert(entry);
    if (!entry)
        return;

    const char* line = entry->m_line.get();

    // TODO: replace line and undo list; but for now, just replace the line as
    // an undoable step.
    g_tib->begin_undo_group();
    g_tib->remove_text(0, -1);
    g_tib->insert_text(line);
    // TODO: might need to set caret here in order for undo to work intuitively.
    g_tib->end_undo_group();

    if (unsave_list)
        unsave_list->clear_saved_entry();
}

//------------------------------------------------------------------------------
void replace_from_history(int32 history_index)
{
    history_list* list = history_list::get();
// TODO: need to track history search index and use that.
    const auto old_index = list->get_index();

    list->set_index(history_index);
    history_index = list->get_index();

    if (old_index == history_index)
        return;

    const auto history_length = list->length();
    const hist_entry* entry = nullptr;

    if (history_length != history_index)
    {
        if (old_index == list->length())
        {
            // TODO: undo stack.
            list->save_entry(g_tib->get_text().c_str());
        }
        entry = list->get_entry(history_index);
    }

    replace_from_history(entry);

    suppress_suggestions();
}
