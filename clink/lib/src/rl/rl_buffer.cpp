// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_buffer.h"
#include "line_state.h"

#include <core/base.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>

extern "C" {
#include <readline/history.h>
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
rl_buffer::rl_buffer(const char *command_delims,
                     const char *word_delims,
                     const char *quote_pair)
: m_command_delims(command_delims)
, m_word_delims(word_delims)
, m_quote_pair(quote_pair)
{
}

//------------------------------------------------------------------------------
void rl_buffer::reset()
{
    using_history();
    remove(0, ~0u);
}

//------------------------------------------------------------------------------
void rl_buffer::begin_line()
{
    m_need_draw = true;
}

//------------------------------------------------------------------------------
void rl_buffer::end_line()
{
}

//------------------------------------------------------------------------------
const char* rl_buffer::get_buffer() const
{
    return rl_line_buffer;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::get_length() const
{
    return rl_end;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::get_cursor() const
{
    return rl_point;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::set_cursor(unsigned int pos)
{
    return rl_point = min<unsigned int>(pos, rl_end);
}

//------------------------------------------------------------------------------
bool rl_buffer::insert(const char* text)
{
    return (m_need_draw = (text[rl_insert_text(text)] == '\0'));
}

//------------------------------------------------------------------------------
bool rl_buffer::remove(unsigned int from, unsigned int to)
{
    to = min(to, get_length());
    m_need_draw = !!rl_delete_text(from, to);
    set_cursor(get_cursor());
    return m_need_draw;
}

//------------------------------------------------------------------------------
void rl_buffer::draw()
{
    if (m_need_draw)
    {
        rl_redisplay();
        m_need_draw = false;
    }
}

//------------------------------------------------------------------------------
void rl_buffer::redraw()
{
    rl_forced_update_display();
}

//------------------------------------------------------------------------------
void rl_buffer::begin_undo_group()
{
    rl_begin_undo_group();
}

//------------------------------------------------------------------------------
void rl_buffer::end_undo_group()
{
    rl_end_undo_group();
}

//------------------------------------------------------------------------------
void rl_buffer::find_command_bounds(std::vector<command>& commands, bool stop_at_cursor) const
{
    const char* line_buffer = get_buffer();
    unsigned int line_stop = stop_at_cursor ? get_cursor() : get_length();

    commands.clear();

    if (m_command_delims == nullptr)
    {
        commands.push_back({ 0, line_stop });
        return;
    }

    str_iter token_iter(line_buffer, line_stop);
    str_tokeniser tokens(token_iter, m_command_delims);
    tokens.add_quote_pair(m_quote_pair);

    const char* start;
    int length;
    while (tokens.next(start, length))
    {
        unsigned int offset = unsigned(start - line_buffer);
        if (stop_at_cursor)
        {
            // Have we found the command containing the cursor?
            if (get_cursor() >= offset &&
                get_cursor() <= offset + length)
            {
                commands.push_back({ offset, unsigned(length) });
                return;
            }
        }
        else
        {
            commands.push_back({ offset, unsigned(length) });
        }
    }

    if (stop_at_cursor)
    {
        // We should expect to reach the cursor. If not then there's a trailing
        // separator and we'll just say the command starts at the cursor.
        start = line_buffer + line_stop;
        length = 0;
    }
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::collect_words(std::vector<word>& words, bool stop_at_cursor) const
{
    words.clear();

    const char* line_buffer = get_buffer();
    unsigned int line_cursor = get_cursor();

    std::vector<command> commands;
    find_command_bounds(commands, stop_at_cursor);

    unsigned int command_offset = 0;

    for (auto& command : commands)
    {
        bool first = true;
        command_offset = command.offset;

        str_iter token_iter(line_buffer + command.offset, command.length);
        str_tokeniser tokens(token_iter, m_word_delims);
        tokens.add_quote_pair(m_quote_pair);
        while (1)
        {
            int length = 0;
            const char *start = nullptr;
            str_token token = tokens.next(start, length);
            if (!token)
                break;

            // Add the word.
            unsigned int offset = unsigned(start - line_buffer);
            words.push_back({offset, unsigned(length), first, 0, token.delim});

            first = false;
        }
    }

    // Add an empty word if no words, or if stopping at the cursor and it's at
    // the beginning of a word.
    word* end_word = words.empty() ? nullptr : &words.back();
    if (!end_word || (stop_at_cursor && end_word->offset + end_word->length < line_cursor))
    {
        words.push_back({ line_cursor, 0, !end_word, 0, 0 });
    }

    // Adjust for quotes.
    for (word& word : words)
    {
        if (word.length == 0)
            continue;

        const char* start = line_buffer + word.offset;

        int start_quoted = (start[0] == m_quote_pair[0]);
        int end_quoted = 0;
        if (word.length > 1)
            end_quoted = (start[word.length - 1] == get_closing_quote());

        word.offset += start_quoted;
        word.length -= start_quoted + end_quoted;
        word.quoted = !!start_quoted;
    }

    return command_offset;
}
