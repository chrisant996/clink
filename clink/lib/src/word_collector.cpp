// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "word_collector.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <core/os.h>

#include <vector>
#include <memory>

//------------------------------------------------------------------------------
simple_word_tokeniser::simple_word_tokeniser(const char* delims)
: m_delims(delims)
{
}

//------------------------------------------------------------------------------
simple_word_tokeniser::~simple_word_tokeniser()
{
    delete m_tokeniser;
}

//------------------------------------------------------------------------------
void simple_word_tokeniser::start(const str_iter& iter, const char* quote_pair)
{
    delete m_tokeniser;
    m_start = iter.get_pointer();
    m_tokeniser = new str_tokeniser(iter, m_delims);
    m_tokeniser->add_quote_pair(quote_pair);
}

//------------------------------------------------------------------------------
word_token simple_word_tokeniser::next(unsigned int& offset, unsigned int& length)
{
    const char* ptr;
    int len;
    str_token token = m_tokeniser->next(ptr, len);
    if (!token)
        return word_token(word_token::invalid_delim);

    offset = static_cast<unsigned int>(ptr - m_start);
    length = len;
    return word_token(token.delim);
}



//------------------------------------------------------------------------------
word_collector::word_collector(collector_tokeniser* command_tokeniser, collector_tokeniser* word_tokeniser, const char* quote_pair)
: m_command_tokeniser(command_tokeniser)
, m_word_tokeniser(word_tokeniser)
, m_quote_pair((quote_pair && *quote_pair) ? quote_pair : "\"")
{
    if (!m_word_tokeniser)
    {
        m_word_tokeniser = new simple_word_tokeniser();
        m_delete_word_tokeniser = true;
    }
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

    if (m_command_tokeniser == nullptr)
    {
        commands.push_back({ 0, line_stop });
        return;
    }

    m_command_tokeniser->start(str_iter(buffer, line_stop), m_quote_pair);
    unsigned int command_start;
    unsigned int command_length;
    while (m_command_tokeniser->next(command_start, command_length))
    {
        if (stop_at_cursor)
        {
            // Have we found the command containing the cursor?
            if (cursor >= command_start &&
                cursor <= command_start + command_length)
            {
                commands.push_back({ command_start, command_length });
                return;
            }
        }
        else
        {
            commands.push_back({ command_start, command_length });
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
                    words.push_back({command.offset, doskey_len, first, true/*is_alias*/, false/*is_redir_arg*/, 0, delim});
                    first = false;
                }
            }
        }

        m_word_tokeniser->start(str_iter(line_buffer + command.offset + doskey_len, command.length - doskey_len), m_quote_pair);
        while (1)
        {
            unsigned int word_offset = 0;
            unsigned int word_length = 0;
            word_token token = m_word_tokeniser->next(word_offset, word_length);
            if (!token)
                break;

            word_offset += command.offset + doskey_len;
            const char* word_start = line_buffer + word_offset;

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
            // widespread use.
            //
            // If the word starts with / or - the word gets special treatment:
            //  - When = immediately follows the end of the word, it is added to
            //    the word.
            //  - When : is reached, it splits the word.
            if (!token.redir_arg && word_length > 1 && strchr("-/", *word_start))
            {
                str_iter split_iter(word_start, word_length);
                while (int c = split_iter.next())
                {
                    if (c == ':')
                    {
                        const unsigned int split_len = unsigned(split_iter.get_pointer() - word_start);
                        words.push_back({word_offset, split_len, first, false/*is_alias*/, false/*is_redir_arg*/, 0, ':'});
                        word_offset += split_len;
                        word_length -= split_len;
                        first = false;
                        break;
                    }
                    else if (!split_iter.more())
                    {
                        while (word_offset + word_length < command.offset + command.length &&
                               line_buffer[word_offset + word_length] == '=')
                        {
                            word_length++;
                        }
                    }
                }
            }

            // Add the word.
            words.push_back({word_offset, unsigned(word_length), first, false/*is_alias*/, token.redir_arg, 0, token.delim});

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
    if (dbg_get_env_int("DEBUG_COLLECTWORDS") < 0)
    {
        int i = 0;
        for (word& word : words)
        {
            str<> tmp;
            str_iter it(line_buffer + word.offset, word.length);
            tmp.concat(it.get_pointer(), it.length());
            printf("WORD %d '%s'\n", i, tmp.c_str());
            i++;
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
