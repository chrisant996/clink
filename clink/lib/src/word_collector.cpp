// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "word_collector.h"
#include "alias_cache.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <core/str_map.h>
#include <core/linear_allocator.h>
#include <core/auto_free_str.h>
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
word_collector::~word_collector()
{
    delete m_alias_cache;
    if (m_delete_word_tokeniser)
        delete m_word_tokeniser;
}

//------------------------------------------------------------------------------
void word_collector::init_alias_cache()
{
    if (!m_alias_cache)
        m_alias_cache = new alias_cache;
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
        commands.push_back({ command_start, command_length });

        // Have we found the command containing the cursor?
        if (stop_at_cursor && (cursor >= command_start &&
                               cursor <= command_start + command_length))
            return;
    }
}

//------------------------------------------------------------------------------
bool word_collector::get_alias(const char* name, str_base& out) const
{
    if (m_alias_cache)
        return m_alias_cache->get_alias(name, out);
    return os::get_alias(name, out);
}

//------------------------------------------------------------------------------
unsigned int word_collector::collect_words(const char* line_buffer, unsigned int line_length, unsigned int line_cursor,
                                           std::vector<word>& words, collect_words_mode mode) const
{
    words.clear();

    std::vector<command> commands;
    commands.reserve(5);
    const bool stop_at_cursor = (mode == collect_words_mode::stop_at_cursor);
    find_command_bounds(line_buffer, line_length, line_cursor, commands, stop_at_cursor);

    unsigned int command_offset = 0;

    for (auto& command : commands)
    {
        bool first = true;
        unsigned int doskey_len = 0;
        bool deprecated_argmatcher = false;

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
                if (get_alias(lookup.c_str(), alias))
                {
                    unsigned char delim = (doskey_len < command.length) ? line_buffer[command.offset + doskey_len] : 0;
                    doskey_len = first_word_len;
                    words.push_back({command.offset, doskey_len, first, true/*is_alias*/, false/*is_redir_arg*/, 0, delim});
                    first = false;
                }

                if (m_command_tokeniser)
                    deprecated_argmatcher = m_command_tokeniser->has_deprecated_argmatcher(lookup.c_str());
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

            // Plus sign is never a word break immediately after a space.
            if (word_offset >= 2 &&
                line_buffer[word_offset - 1] == '+' &&
                line_buffer[word_offset - 2] == ' ')
            {
                word_offset--;
                word_length++;
            }

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
            //
            // But not for deprecated argmatchers:
            // https://github.com/chrisant996/clink/issues/174
            // An argmatcher may have used an args function to provide flags
            // like "-D:Aoption", "-D:Boption", etc, in which case `:` and `=`
            // should not be word breaks.
            if (!token.redir_arg &&
                !deprecated_argmatcher &&
                word_length > 1 &&
                strchr("-/", *word_start))
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
        words.push_back({line_cursor, 0, !end_word});
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
        for (const word& word : words)
        {
            str<> tmp;
            str_iter it(line_buffer + word.offset, word.length);
            tmp.concat(it.get_pointer(), it.length());
            printf("WORD %d '%s'%s%s\n", i, tmp.c_str(), word.is_redir_arg ? " redir" : "", word.command_word ? " command" : "");
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

//------------------------------------------------------------------------------
void commands::set(const char* line_buffer, unsigned int line_length, unsigned int line_cursor, const std::vector<word>& words)
{
    clear_internal();

    // Count number of commands so we can pre-allocate words_storage so that
    // emplace_back() doesn't invalidate pointers (references) stored in
    // linestates.
    unsigned int num_commands = 0;
    for (const auto& word : words)
    {
        if (word.command_word)
            num_commands++;
    }

    // Build vector containing one line_state per command.
    size_t i = 0;
    std::vector<word> tmp;
    tmp.reserve(words.size());
    m_words_storage.reserve(num_commands);
    while (true)
    {
        if (!tmp.empty() && (i >= words.size() || words[i].command_word))
        {
            // Make sure classifiers can tell whether the word has a space
            // before it, so that ` doskeyalias` gets classified as NOT a doskey
            // alias, since doskey::resolve() won't expand it as a doskey alias.
            int command_char_offset = tmp[0].offset;
            if (command_char_offset == 1 && line_buffer[0] == ' ')
                command_char_offset--;
            else if (command_char_offset >= 2 &&
                     line_buffer[command_char_offset - 1] == ' ' &&
                     line_buffer[command_char_offset - 2] == ' ')
                command_char_offset--;

            m_words_storage.emplace_back(std::move(tmp));
            assert(tmp.empty());
            tmp.reserve(words.size());

            m_linestates.emplace_back(
                line_buffer,
                line_length,
                line_cursor,
                command_char_offset,
                m_words_storage.back()
            );
        }

        if (i >= words.size())
            break;

        tmp.emplace_back(words[i]);
        i++;
    }

    if (m_words_storage.size() > 0)
    {
        // Guarantee room for get_word_break_info() to append an empty end word.
        std::vector<word>& last = m_words_storage.back();
        last.reserve(last.size() + 1);
    }
}

//------------------------------------------------------------------------------
void commands::set(const line_buffer& buffer, const std::vector<word>& words)
{
    set(buffer.get_buffer(), buffer.get_length(), buffer.get_cursor(), words);
}

//------------------------------------------------------------------------------
unsigned int commands::break_end_word(unsigned int truncate, unsigned int keep)
{
#ifdef DEBUG
    assert(!m_broke_end_word);
    m_broke_end_word = true;
#endif

    word* end_word = const_cast<word*>(&m_linestates.back().get_words().back());
    if (truncate)
    {
        truncate = min<unsigned int>(truncate, end_word->length);

        word split_word;
        split_word.offset = end_word->offset + truncate;
        split_word.length = end_word->length - truncate;
        split_word.command_word = false;
        split_word.is_alias = false;
        split_word.is_redir_arg = false;
        split_word.quoted = false;
        split_word.delim = str_token::invalid_delim;

        std::vector<word>* words = const_cast<std::vector<word>*>(&m_words_storage.back());
        end_word->length = truncate;
        words->push_back(split_word);
        end_word = &words->back();
    }

    keep = min<unsigned int>(keep, end_word->length);
    end_word->length = keep;
    return end_word->offset;
}

//------------------------------------------------------------------------------
void commands::clear()
{
    clear_internal();

    m_words_storage.emplace_back();
    m_linestates.emplace_back(line_state {
        nullptr,
        0,
        0,
        0,
        m_words_storage[0]
    });
}

//------------------------------------------------------------------------------
void commands::clear_internal()
{
    m_words_storage.clear();
    m_linestates.clear();
#ifdef DEBUG
    m_broke_end_word = false;
#endif
}

//------------------------------------------------------------------------------
const line_states& commands::get_linestates(const char* buffer, unsigned int len) const
{
    assert(m_linestates.size());
    const auto& front = m_linestates.front();
    if (buffer != front.get_line() || len != front.get_length())
    {
        static line_states* s_none = nullptr;
        if (!s_none)
        {
            std::vector<word>* wv = new std::vector<word>;
            s_none = new line_states;
            s_none->push_back({ nullptr, 0, 0, 0, *wv });
        }
        return *s_none;
    }
    return m_linestates;
}

//------------------------------------------------------------------------------
const line_states& commands::get_linestates(const line_buffer& buffer) const
{
    return get_linestates(buffer.get_buffer(), buffer.get_length());
}

//------------------------------------------------------------------------------
const line_state& commands::get_linestate(const char* buffer, unsigned int len) const
{
    assert(m_linestates.size());
    const auto& back = m_linestates.back();
    if (buffer != back.get_line() || len != back.get_length())
    {
        static line_state* s_none = nullptr;
        if (!s_none)
        {
            std::vector<word>* wv = new std::vector<word>;
            s_none = new line_state(nullptr, 0, 0, 0, *wv);
        }
        return *s_none;
    }
    return back;
}

//------------------------------------------------------------------------------
const line_state& commands::get_linestate(const line_buffer& buffer) const
{
    return get_linestate(buffer.get_buffer(), buffer.get_length());
}
