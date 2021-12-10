// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum class suggestion_action : unsigned char
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
    bool            get_visible(str_base& out) const;
    void            clear();
    bool            can_suggest(line_state& line);
    void            set(line_state& line, const char* suggestion, unsigned int offset);
    bool            insert(suggestion_action action);
    void            update_endword_offset(line_state& line);

private:
    void            resync_suggestion_iterator(unsigned int old_cursor);
    str_iter        m_iter;
    str_moveable    m_suggestion;
    str_moveable    m_line;         // Input line that generated the suggestion.
    unsigned int    m_suggestion_offset = -1;
    unsigned int    m_endword_offset = -1;
};

//------------------------------------------------------------------------------
bool insert_suggestion(suggestion_action action);
