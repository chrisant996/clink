// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "word_collector.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <core/os.h>

#include <vector>

//------------------------------------------------------------------------------
word_collector::word_collector(const char* command_delims, const char* word_delims, const char* quote_pair)
: m_command_delims(command_delims)
, m_word_delims(word_delims)
, m_quote_pair(quote_pair)
{
}

//------------------------------------------------------------------------------
char word_collector::get_opening_quote() const
{
    return m_quote_pair[0];
}

//------------------------------------------------------------------------------
char word_collector::get_closing_quote() const
{
    return m_quote_pair[1] ? m_quote_pair[1] : m_quote_pair[0];
}

//------------------------------------------------------------------------------
void word_collector::find_command_bounds(const char* buffer, unsigned int length, unsigned int cursor,
                                         std::vector<command>& commands, bool stop_at_cursor) const
{
    unsigned int line_stop = stop_at_cursor ? cursor : length;

    commands.clear();

    if (m_command_delims == nullptr)
    {
        commands.push_back({ 0, line_stop });
        return;
    }

    str_iter token_iter(buffer, line_stop);
    str_tokeniser tokens(token_iter, m_command_delims);
    tokens.add_quote_pair(m_quote_pair);

    const char* command_start;
    int command_length;
    while (tokens.next(command_start, command_length))
    {
        // Match the doskey-disabler space in doskey::resolve().
        if (command_start > buffer && command_length && command_start[0] == ' ')
            command_start++, command_length--;

        unsigned int offset = unsigned(command_start - buffer);
        if (stop_at_cursor)
        {
            // Have we found the command containing the cursor?
            if (cursor >= offset &&
                cursor <= offset + command_length)
            {
                commands.push_back({ offset, unsigned(command_length) });
                return;
            }
        }
        else
        {
            commands.push_back({ offset, unsigned(command_length) });
        }
    }
}

//------------------------------------------------------------------------------
unsigned int word_collector::collect_words(const char* line_buffer, unsigned int line_length, unsigned int line_cursor,
                                           std::vector<word>& words, collect_words_mode mode) const
{
    words.clear();

    std::vector<command> commands;
    bool stop_at_cursor = (mode == collect_words_mode::stop_at_cursor ||
                           mode == collect_words_mode::display_filter);
    find_command_bounds(line_buffer, line_length, line_cursor, commands, stop_at_cursor);

    unsigned int command_offset = 0;

    for (auto& command : commands)
    {
        bool first = true;
        unsigned int doskey_len = 0;

        if (line_cursor >= command.offset)
            command_offset = command.offset;

        {
            unsigned int first_word_len = 0;
            while (first_word_len < command.length &&
                    line_buffer[command.offset + first_word_len] != ' ' &&
                    line_buffer[command.offset + first_word_len] != '\t')
                first_word_len++;

            if (first_word_len > 0)
            {
                str<32> lookup;
                str<32> alias;
                lookup.concat(line_buffer + command.offset, first_word_len);
                if (os::get_alias(lookup.c_str(), alias))
                {
                    unsigned char delim = (doskey_len < command.length) ? line_buffer[command.offset + doskey_len] : 0;
                    doskey_len = first_word_len;
                    words.push_back({command.offset, doskey_len, first, true/*is_alias*/, 0, delim});
                    first = false;
                }
            }
        }

        str_iter token_iter(line_buffer + command.offset + doskey_len, command.length - doskey_len);
        str_tokeniser tokens(token_iter, m_word_delims);
        tokens.add_quote_pair(m_quote_pair);
        while (1)
        {
            int word_length = 0;
            const char *word_start = nullptr;
            str_token token = tokens.next(word_start, word_length);
            if (!token)
                break;

            unsigned int offset = unsigned(word_start - line_buffer);

            // Mercy.  We need to know later on if a flag word ends with = but
            // that's never part of a word because it's a word delimiter.  We
            // can't really know what is a flag word without running argmatchers
            // because the argmatchers define the flag character(s) (and linked
            // argmatchers can define different flag characters).  But we can't
            // run argmatchers without having already parsed the words.  The
            // abstraction between collecting words and running argmatchers
            // breaks down here.
            //
            // Rather that redesign the system or dream up a complex solution,
            // we'll use a simple(ish) mitigation that works the vast majority
            // of the time because / and - are the only flag characters in
            // widespread use.  If the word starts with / or - and the next
            // character in the line is = then add it to the word.
            if (word_length > 1 && strchr("-/", line_buffer[offset]))
            {
                while (offset + word_length < command.offset + command.length &&
                    line_buffer[offset + word_length] == '=')
                {
                    word_length++;
                }
            }

            // Add the word.
            words.push_back({offset, unsigned(word_length), first, false/*is_alias*/, 0, token.delim});

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
        if (word.length == 0 || word.is_alias)
            continue;

        const char* start = line_buffer + word.offset;

        int start_quoted = (start[0] == get_opening_quote());
        int end_quoted = 0;
        if (word.length > 1)
            end_quoted = (start[word.length - 1] == get_closing_quote());

        word.offset += start_quoted;
        word.length -= start_quoted + end_quoted;
        word.quoted = !!start_quoted;
    }

#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_COLLECTWORDS"))
    {
        for (word& word : words)
        {
            str<> tmp;
            str_iter it(line_buffer + word.offset, word.length);
            tmp.concat(it.get_pointer(), it.length());
            printf("WORD '%s'\n", tmp.c_str());
        }
    }
#endif

    return command_offset;
}

//------------------------------------------------------------------------------
unsigned int word_collector::collect_words(const line_buffer& buffer, std::vector<word>& words, collect_words_mode mode) const
{
    return collect_words(buffer.get_buffer(), buffer.get_length(), buffer.get_cursor(), words, mode);
}
