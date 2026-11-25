// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/auto_free_str.h>
#include <core/str_iter.h>
#include <core/singleton.h>

#include <memory>
#include <vector>

//------------------------------------------------------------------------------
struct hist_entry
{
    auto_free_str               m_line;
    auto_free_str               m_timestamp;
    // TODO: pointer to undo stack.
};

//------------------------------------------------------------------------------
typedef std::vector<std::unique_ptr<hist_entry>> history_entries;

//------------------------------------------------------------------------------
class history_list
{
    const uint32 c_max_capacity = 999999;

public:
                                history_list() = default;
                                ~history_list() = default;

    // List management.
    void                        clear();
    bool                        add_history(const char* line, const char* timestamp=nullptr);
    bool                        remove_history(uint32 index);

    // Cursoring.
    uint32                      get_count() const { return uint32(m_entries.size()); }
    uint32                      get_index() const { return m_index; }
    bool                        set_index(uint32 index);
    const hist_entry*           get_entry(uint32 index) const;
    void                        reset_cursor() { m_index = get_count(); }

    // Navigation.

    // Searching.

private:
    uint32                      m_index = 0;
    history_entries             m_entries;
};

//------------------------------------------------------------------------------
class history : public history_list, public singleton<history>
{
public:
    history() = default;
    ~history() = default;
};
