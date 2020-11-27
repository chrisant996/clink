// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state.h"

#include <core/str.h>

#include <vector>

//------------------------------------------------------------------------------
line_state::line_state(
    const char* line,
    unsigned int cursor,
    unsigned int command_offset,
    const std::vector<word>& words)
: m_words(words)
, m_line(line)
, m_cursor(cursor)
, m_command_offset(command_offset)
{
}

//------------------------------------------------------------------------------
const char* line_state::get_line() const
{
    return m_line;
}

//------------------------------------------------------------------------------
unsigned int line_state::get_cursor() const
{
    return m_cursor;
}

//------------------------------------------------------------------------------
unsigned int line_state::get_command_offset() const
{
    return m_command_offset;
}

//------------------------------------------------------------------------------
const std::vector<word>& line_state::get_words() const
{
    return m_words;
}

//------------------------------------------------------------------------------
unsigned int line_state::get_word_count() const
{
    return (unsigned int)m_words.size();
}

//------------------------------------------------------------------------------
bool line_state::get_word(unsigned int index, str_base& out) const
{
    if (index >= m_words.size())
        return false;

    const word& word = m_words[index];
    out.concat(m_line + word.offset, word.length);
    return true;
}

//------------------------------------------------------------------------------
str_iter line_state::get_word(unsigned int index) const
{
    if (index < m_words.size())
    {
        const word& word = m_words[index];
        return str_iter(m_line + word.offset, word.length);
    }

    return str_iter();
}

//------------------------------------------------------------------------------
bool line_state::get_end_word(str_base& out) const
{
    int n = get_word_count();
    return (n ? get_word(n - 1, out) : false);
}

//------------------------------------------------------------------------------
str_iter line_state::get_end_word() const
{
    int n = get_word_count();
    return (n ? get_word(n - 1) : str_iter());
}
