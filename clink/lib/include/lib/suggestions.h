// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_buffer.h"

#include <core/str_iter.h>

#include <vector>

//------------------------------------------------------------------------------
enum class suggestion_action : uint8
{
    insert_to_end,
    insert_next_word,
    insert_next_full_word,
};

//------------------------------------------------------------------------------
struct suggestion
{
    str_moveable    m_suggestion;
    uint32          m_suggestion_offset = -1;
    str_moveable    m_source;
};

//------------------------------------------------------------------------------
class suggestions
{
    friend class suggestion_manager;
public:
                    suggestions() = default;
    suggestions&    operator = (const suggestions& other);
    suggestions&    operator = (suggestions&& other);
    bool            is_same(const suggestions& other) const;
    void            clear(uint32 generation_id=0);
    bool            empty() const { return m_items.empty(); }
    size_t          size() const { return m_items.size(); }
    const str_moveable& get_line() const { return m_line; }
    void            set_line(const char* line, int32 length=-1);
    void            add(const char* text, uint32 offset, const char* source);
    const suggestion& operator [] (uint32 index) const { return m_items[index]; }
    const suggestion& get(uint32 index) const { return m_items[index]; }
    void            remove(uint32 index);
    uint32          get_generation_id() const { return m_generation_id; }
private:
    str_moveable    m_line;         // Input line off which suggestions are based.
    std::vector<suggestion> m_items;
    uint32          m_generation_id = 0;
};

//------------------------------------------------------------------------------
class suggestion_manager
{
public:
    bool            more() const;
    bool            get_visible(str_base& out, bool* includes_hint=nullptr) const;
    bool            has_suggestion() const;
    void            clear();
    bool            can_suggest(const line_state& line);
    bool            can_update_matches();
    bool            is_locked_against_suggestions();
    void            lock_against_suggestions(bool lock);
    bool            is_suppressing_suggestions() const { return m_suppress; }
    void            suppress_suggestions();
    void            set_started(const char* line);
    void            set(const char* line, uint32 endword_offset, suggestions* suggestions);
    bool            get(suggestions& out);
    bool            insert(suggestion_action action);
    bool            pause(bool pause);

private:
    void            resync_suggestion_iterator(uint32 old_cursor);
    str_iter        m_iter;
    str_moveable    m_started;          // Input line that started generating a suggestion.
    line_buffer_fingerprint m_locked;   // Locks against generating suggestions for this buffer generation ID.
    suggestions     m_suggestions;
    uint32          m_endword_offset = -1;
    bool            m_paused = false;
    bool            m_suppress = false; // Locks and also keeps it locked after backspace-like commands.

    static uint32   new_generation();
    static uint32   s_generation_id;
};

//------------------------------------------------------------------------------
bool has_suggestion();
bool is_locked_against_suggestions();
extern "C" void lock_against_suggestions(int lock);
extern "C" void clear_suggestion();
bool can_show_suggestion_hint();
void suppress_suggestions();
void set_suggestion_started(const char* line);
void set_suggestions(const char* line, uint32 endword_offset, suggestions* suggestions);
bool get_suggestions(suggestions& out);
bool insert_suggestion(suggestion_action action);
bool pause_suggestions(bool pause);
