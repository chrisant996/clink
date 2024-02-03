// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum class suggestion_action : uint8
{
    insert_to_end,
    insert_next_word,
    insert_next_full_word,
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
    bool            is_suppressing_suggestions() const { return m_suppress; }
    void            suppress_suggestions();
    void            set_started(const char* line);
    void            set(const char* line, uint32 endword_offset, const char* suggestion, uint32 offset);
    bool            insert(suggestion_action action);
    bool            pause(bool pause);

private:
    void            resync_suggestion_iterator(uint32 old_cursor);
    str_iter        m_iter;
    str_moveable    m_suggestion;
    str_moveable    m_line;         // Input line that generated the suggestion.
    str_moveable    m_started;      // Input line that started generating a suggestion.
    uint32          m_suggestion_offset = -1;
    uint32          m_endword_offset = -1;
    bool            m_paused = false;
    bool            m_suppress = false;
};

//------------------------------------------------------------------------------
bool has_suggestion();
extern "C" void clear_suggestion();
bool can_show_suggestion_hint();
void suppress_suggestions();
void set_suggestion_started(const char* line);
void set_suggestion(const char* line, uint32 endword_offset, const char* suggestion, uint32 offset);
bool insert_suggestion(suggestion_action action);
bool pause_suggestions(bool pause);
