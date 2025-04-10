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
static bool s_can_strip_quotes = false;

//------------------------------------------------------------------------------
word::word(uint32 _offset, uint32 _length, bool _command_word, bool _is_alias, bool _is_redir_arg, bool _quoted, uint8 _delim)
: offset(_offset)
, length(_length)
, command_word(_command_word)
, is_alias(_is_alias)
, is_redir_arg(_is_redir_arg)
, is_merged_away(false)
, quoted(_quoted)
, delim(_delim)
{
}

//------------------------------------------------------------------------------
line_state::line_state(
    const char* line,
    uint32 length,
    uint32 cursor,
    uint32 words_limit,
    uint32 command_offset,
    uint32 range_offset,
    uint32 range_length,
    const std::vector<word>& words)
: m_words(words)
, m_line(line)
, m_length(length)
, m_cursor(cursor)
, m_words_limit(words_limit)
, m_command_offset(command_offset)
, m_range_offset(range_offset)
, m_range_length(range_length)
{
}

//------------------------------------------------------------------------------
const char* line_state::get_line() const
{
    return m_line;
}

//------------------------------------------------------------------------------
uint32 line_state::get_length() const
{
    return m_length;
}

//------------------------------------------------------------------------------
uint32 line_state::get_cursor() const
{
    return m_cursor;
}

//------------------------------------------------------------------------------
uint32 line_state::get_words_limit() const
{
    return m_words_limit;
}

//------------------------------------------------------------------------------
uint32 line_state::get_command_offset() const
{
    return m_command_offset;
}

//------------------------------------------------------------------------------
uint32 line_state::get_command_word_index() const
{
    uint32 i = 0;
    while (i < m_words.size())
    {
        if (!m_words[i].is_redir_arg)
            break;
        i++;
    }
    return i;
}

//------------------------------------------------------------------------------
uint32 line_state::get_end_word_offset() const
{
    if (m_words.size() > 0)
        return m_words.back().offset;
    return 0;
}

//------------------------------------------------------------------------------
uint32 line_state::get_range_offset() const
{
    return m_range_offset;
}

//------------------------------------------------------------------------------
uint32 line_state::get_range_length() const
{
    return m_range_length;
}

//------------------------------------------------------------------------------
const std::vector<word>& line_state::get_words() const
{
    return m_words;
}

//------------------------------------------------------------------------------
uint32 line_state::get_word_count() const
{
    return (uint32)m_words.size();
}

//------------------------------------------------------------------------------
bool line_state::get_word(uint32 index, str_base& out) const
{
    // MAY STRIP quotes.
    if (index < m_words.size())
    {
        const word& word = m_words[index];
        if (s_can_strip_quotes)
        {
            // Strip quotes so `"foo\"ba` can complete to `"foo\bar"`.
            // Stripping quotes may seem surprising, but it's what CMD does
            // and it works well.
            concat_strip_quotes(out, m_line + word.offset, word.length);
        }
        else
        {
            out.clear();
            out.concat(m_line + word.offset, word.length);
        }
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
str_iter line_state::get_word(uint32 index) const
{
    // Never strips quotes.
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
    // MAY STRIP quotes.
    int32 n = get_word_count();
    return (n ? get_word(n - 1, out) : false);
}

//------------------------------------------------------------------------------
str_iter line_state::get_end_word() const
{
    // Never strips quotes.
    int32 n = get_word_count();
    return (n ? get_word(n - 1) : str_iter());
}

//------------------------------------------------------------------------------
bool line_state::overwrite_from(const line_state* other)
{
    assert(other);
    if (!other)
        return false;

    assert(other->m_length == m_length);
    assert(other->m_cursor == m_cursor);
    assert(other->m_command_offset == m_command_offset);
    if (other->m_length != m_length ||
        other->m_cursor != m_cursor ||
        other->m_command_offset != m_command_offset)
        return false;

    assert(strcmp(other->m_line, m_line) == 0);
    if (strcmp(other->m_line, m_line) != 0)
        return false;

    const_cast<std::vector<word>&>(m_words).resize(other->m_words.size());

    size_t resize = 0;
    word* tortoise = const_cast<word*>(&*m_words.begin());
    const word* hare = &*other->m_words.begin();
    for (const word& hare : other->m_words)
    {
        if (hare.is_merged_away)
            continue;
        *tortoise = hare;
        ++tortoise;
        ++resize;
    }

    const_cast<std::vector<word>&>(m_words).resize(resize);

    return true;
}

//------------------------------------------------------------------------------
void line_state::set_can_strip_quotes(bool can)
{
    s_can_strip_quotes = can;
}
