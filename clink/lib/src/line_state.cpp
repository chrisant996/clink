// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <core/os.h>

#include <vector>

//------------------------------------------------------------------------------
line_state::line_state(
    const char* line,
    unsigned int length,
    unsigned int cursor,
    unsigned int command_offset,
    const std::vector<word>& words)
: m_words(words)
, m_line(line)
, m_length(length)
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
unsigned int line_state::get_length() const
{
    return m_length;
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
unsigned int line_state::get_command_word_index() const
{
    unsigned int i = 0;
    while (i < m_words.size())
    {
        if (!m_words[i].is_redir_arg)
            break;
        i++;
    }
    return i;
}

//------------------------------------------------------------------------------
unsigned int line_state::get_end_word_offset() const
{
    if (m_words.size() > 0)
        return m_words.back().offset;
    return 0;
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
    // STRIPS quotes.
    if (index < m_words.size())
    {
        // Strip quotes so `"foo\"ba` can complete to `"foo\bar"`.  Stripping
        // quotes may seem surprising, but it's what CMD does and it works well.
        const word& word = m_words[index];
        concat_strip_quotes(out, m_line + word.offset, word.length);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
str_iter line_state::get_word(unsigned int index) const
{
    // INCLUDES quotes.
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
    // STRIPS quotes.
    int n = get_word_count();
    return (n ? get_word(n - 1, out) : false);
}

//------------------------------------------------------------------------------
str_iter line_state::get_end_word() const
{
    // INCLUDES quotes.
    int n = get_word_count();
    return (n ? get_word(n - 1) : str_iter());
}
