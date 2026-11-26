// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/auto_free_str.h>
#include <core/str.h>
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
    void*                       m_undo = nullptr;
};

//------------------------------------------------------------------------------
typedef std::vector<std::unique_ptr<hist_entry>> history_entries;
constexpr uint32 history_base = 1;

//------------------------------------------------------------------------------
class history_list : public singleton<history_list>
{
    const int32 c_max_capacity = 999999;

public:
                                history_list() = default;
                                ~history_list() = default;

    // List management.
    void                        clear();
    bool                        add_history(const char* line, const char* timestamp=nullptr);
    bool                        remove_history(int32 index);

    // Cursoring.
    void                        reset_cursor();
    int32                       length() const { return int32(m_entries.size()); }
    int32                       get_index() const { return m_index; }
    bool                        set_index(int32 index);
    const hist_entry*           get_entry(int32 index) const;
    const char*                 get_line(int32 index) const;

    // Searching.
    void                        init_search(const char* line, int32 caret);
    bool                        has_search_string() const;
    int32                       search(int32 start, int32 direction, int32& new_caret) const;

    // Saved entry.
    void                        clear_saved_entry();
    bool                        save_entry(const char* line);
    const hist_entry*           get_saved_entry() const;

    // Range loops.
    auto                        begin() const noexcept { return m_entries.cbegin(); }
    auto                        end() const noexcept { return m_entries.cend(); }
    auto                        cbegin() const noexcept { return m_entries.cbegin(); }
    auto                        cend() const noexcept { return m_entries.cend(); }

private:
    int32                       m_index = 0;
    history_entries             m_entries;

    std::unique_ptr<hist_entry> m_saved;

    str_moveable                m_needle;
    mutable const char*         m_last_found = nullptr; // Borrowed from m_entries.
};

//------------------------------------------------------------------------------
void clear_history();
void replace_from_history(int32 history_index);
