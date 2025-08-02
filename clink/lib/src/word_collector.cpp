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
#include <core/settings.h>
#include <core/debugheap.h>

#include <vector>
#include <memory>

extern setting_bool g_enhanced_doskey;
extern setting_enum g_translate_slashes;

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
void simple_word_tokeniser::start(const str_iter& iter, const char* quote_pair, bool at_beginning)
{
    delete m_tokeniser;
    m_start = iter.get_pointer();
    m_tokeniser = new str_tokeniser(iter, m_delims);
    m_tokeniser->add_quote_pair(quote_pair);
}

//------------------------------------------------------------------------------
word_token simple_word_tokeniser::next(uint32& offset, uint32& length)
{
    const char* ptr;
    int32 len;
    str_token token = m_tokeniser->next(ptr, len);

    offset = uint32(ptr - m_start);
    length = len;

    if (!token)
        return word_token(word_token::invalid_delim);

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
void word_collector::find_command_bounds(const char* buffer, uint32 length, uint32 cursor,
                                         std::vector<command>& commands, bool stop_at_cursor) const
{
    const uint32 line_stop = stop_at_cursor ? cursor : length;

    commands.clear();

    if (m_command_tokeniser == nullptr)
    {
        commands.push_back({ 0, line_stop, false });
        return;
    }

    m_command_tokeniser->start(str_iter(buffer, line_stop), m_quote_pair);
    uint32 command_start;
    uint32 command_length;
    while (m_command_tokeniser->next(command_start, command_length))
    {
        commands.push_back({ command_start, command_length, is_alias_allowed(buffer, command_start) });

        // Have we found the command containing the cursor?
        if (stop_at_cursor && (cursor >= command_start &&
                               cursor <= command_start + command_length))
            return;
    }

    // Catch uninitialized variables.
    assert(command_start < 0xccccc);
    assert(command_length < 0xccccc);

    if (!commands.empty())
        return;

    // Need to provide an empty command, because there's an empty command.  For
    // example exec.enable needs this so it can generate matches appropriately.
    commands.push_back({ command_start, command_length, false });
}

//------------------------------------------------------------------------------
bool word_collector::get_alias(const char* name, str_base& out) const
{
    if (m_alias_cache)
        return m_alias_cache->get_alias(name, out);
    return os::get_alias(name, out);
}

//------------------------------------------------------------------------------
bool word_collector::is_alias_allowed(const char* buffer, uint32 offset) const
{
    if (buffer[offset] == ' ')
        return false;

    uint32 max_spaces = 0;
    uint32 spaces = 0;

    while (offset--)
    {
        const char ch = buffer[offset];

        if (ch == '&' || ch == '|')
        {
            if (!g_enhanced_doskey.get())
                return false;
            max_spaces = 1;
            break;
        }

        if (ch != ' ')
            return false;

        ++spaces;
    }

    return (spaces <= max_spaces);
}

//------------------------------------------------------------------------------
uint32 word_collector::collect_words(const char* line_buffer, uint32 line_length, uint32 line_cursor,
                                     std::vector<word>& words, collect_words_mode mode,
                                     std::vector<command>* _commands) const
{
    words.clear();

    std::vector<command> tmp;
    std::vector<command>& commands = _commands ? *_commands : tmp;

    commands.reserve(5);
    const bool stop_at_cursor = (mode == collect_words_mode::stop_at_cursor);
    const uint32 line_stop = stop_at_cursor ? line_cursor : line_length;
    find_command_bounds(line_buffer, line_length, line_cursor, commands, stop_at_cursor);

    uint32 command_offset = 0;

    bool first = true;
    for (const auto& command : commands)
    {
        first = true;

        uint32 doskey_len = 0;
        bool deprecated_argmatcher = false;

        if (line_cursor >= command.offset)
            command_offset = command.offset;

        {
            uint32 first_word_len = 0;
            while (first_word_len < command.length &&
                    line_buffer[command.offset + first_word_len] != ' ' &&
                    line_buffer[command.offset + first_word_len] != '\t')
                first_word_len++;

            if (first_word_len > 0)
            {
                str<32> lookup;
                str<32> alias;
                lookup.concat(line_buffer + command.offset, first_word_len);
                if (command.is_alias_allowed && get_alias(lookup.c_str(), alias))
                {
                    uint8 delim = (doskey_len < command.length) ? line_buffer[command.offset + doskey_len] : 0;
                    doskey_len = first_word_len;
                    words.push_back({command.offset, doskey_len, first, true/*is_alias*/, false/*is_redir_arg*/, 0, delim});
                    first = false;

                    // Consume spaces after the alias, to ensure the tokeniser
                    // doesn't start on a space.  If it does and the rest of
                    // the line is spaces, then the loop will incorrectly add
                    // an empty word, which will make an argmatcher consume an
                    // extra argument slot by mistake when it internally
                    // expands a doskey alias.
                    while (command.offset + doskey_len < line_stop)
                    {
                        const char c = line_buffer[command.offset + doskey_len];
                        if (c != ' ' && c != '\t')
                            break;
                        ++doskey_len;
                    }
                }

                if (m_command_tokeniser)
                    deprecated_argmatcher = m_command_tokeniser->has_deprecated_argmatcher(lookup.c_str());
            }
        }

        assert(command.offset + command.length <= line_stop);
        assert(command.offset + doskey_len <= line_stop);
        assert(command.length >= doskey_len);
        uint32 tokeniser_len = command.length - doskey_len;
        if (!(command.offset + command.length <= line_stop &&
              command.offset + doskey_len <= line_stop &&
              command.length >= doskey_len))
            tokeniser_len = 0;

        m_word_tokeniser->start(str_iter(line_buffer + command.offset + doskey_len, tokeniser_len), m_quote_pair, first);
        while (1)
        {
            uint32 word_offset = 0;
            uint32 word_length = 0;
            word_token token = m_word_tokeniser->next(word_offset, word_length);
            if (!token)
                break;

            word_offset += command.offset + doskey_len;

            // Plus sign is never a word break immediately after a space.
            if (word_offset >= 2 &&
                line_buffer[word_offset - 1] == '+' &&
                line_buffer[word_offset - 2] == ' ')
            {
                word_offset--;
                word_length++;
            }

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
            // Rather than redesign the system or dream up a complex solution,
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
                while (int32 c = split_iter.next())
                {
                    if (c == ':')
                    {
                        const uint32 split_len = unsigned(split_iter.get_pointer() - word_start);
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
            words.push_back(std::move(word(word_offset, unsigned(word_length), first, false/*is_alias*/, token.redir_arg, 0, token.delim)));

            first = false;
        }
    }

    // Special case for "./" and "../" during completion.
    if (stop_at_cursor)
    {
        const int32 translate = g_translate_slashes.get();
        if (translate == 1 || translate == 3)
        {
            const size_t n = words.size();
            if (n >= 2)
            {
                auto& cword = words[n - 2];
                if (cword.command_word && cword.length > 0)
                {
                    const auto& nword = words[n - 1];
                    if (nword.length > 0 &&
                        nword.offset == cword.offset + cword.length &&
                        line_buffer[nword.offset] == '/' &&
                        line_buffer[cword.offset] == '.')
                    {
                        if (cword.length == 1 || (cword.length == 2 && line_buffer[cword.offset + 1] == '.'))
                        {
                            // Merge the command word and the next word.
                            cword.length += nword.length;
                            words.pop_back();
                        }
                    }
                }
            }
        }
    }

    // Add an empty word if no words, or if stopping at the cursor and it's at
    // the beginning of a word.
    word* end_word = words.empty() ? nullptr : &words.back();
    if (!end_word || (stop_at_cursor && end_word->offset + end_word->length < line_cursor))
    {
        uint8 delim = 0;
        if (line_cursor)
            delim = line_buffer[line_cursor - 1];

        words.push_back(std::move(word(line_cursor, 0, first, false, false, false, delim)));
    }

    // Adjust for quotes.
    for (word& word : words)
    {
        if (word.length == 0 || word.is_alias)
            continue;

        const char* start = line_buffer + word.offset;

        int32 start_quoted = (start[0] == get_opening_quote());
        int32 end_quoted = 0;
        if (start_quoted && word.length > 1 && start[word.length - 1] == get_closing_quote())
        {
            bool quoted = true;
            uint32 last_end_quote = 0;
            for (uint32 i = 1; i < word.length; ++i)
            {
                if (start[i] == '"')
                {
                    if (quoted)
                        last_end_quote = i;
                    quoted = !quoted;
                }
                else if (!quoted && start[i] == '^')
                {
                    ++i;
                }
            }
            end_quoted = (!quoted && last_end_quote + 1 == word.length);
        }

        word.offset += start_quoted;
        word.length -= start_quoted + end_quoted;
        word.quoted = !!start_quoted;
    }

#ifdef DEBUG
    if (dbg_get_env_int(stop_at_cursor ? "DEBUG_COLLECTWORDS" : "DEBUG_COLLECTWORDS_CLASSIFY") < 0)
    {
        int32 i = 0;
        if (words.size() > 0)
            printf("collect words (%s):\n", stop_at_cursor ? "stop_at_cursor" : "whole_command");
        for (const word& word : words)
        {
            str<> tmp;
            str_iter it(line_buffer + word.offset, word.length);
            tmp.concat(it.get_pointer(), it.length());
            printf("WORD %d '%s'%s%s (delim '%.1s')\n", i, tmp.c_str(), word.is_redir_arg ? " redir" : "", word.command_word ? " command" : "", word.delim ? reinterpret_cast<const char*>(&word.delim) : "");
            i++;
        }
    }
#endif

    return command_offset;
}

//------------------------------------------------------------------------------
uint32 word_collector::collect_words(const line_buffer& buffer, std::vector<word>& words, collect_words_mode mode, std::vector<command>* commands) const
{
    return collect_words(buffer.get_buffer(), buffer.get_length(), buffer.get_cursor(), words, mode, commands);
}

//------------------------------------------------------------------------------
void command_line_states::set(const char* line_buffer,
                              uint32 line_length,
                              uint32 line_cursor,
                              const std::vector<word>& words,
                              collect_words_mode mode,
                              const std::vector<command>& commands)
{
    clear_internal();

    // Pre-allocate words_storage so that emplace_back() doesn't invalidate
    // pointers (references) stored in linestates.
    m_words_storage.reserve(commands.size());

    // Build vector containing one line_state per command.
    size_t i = 0;
    auto command_iter = commands.begin();
    std::vector<word> tmp;
    tmp.reserve(words.size());
    while (true)
    {
        if (!tmp.empty() && (i >= words.size() || words[i].command_word))
        {
            // Make sure classifiers can tell whether the word has a space
            // before it, so that ` doskeyalias` gets classified as NOT a doskey
            // alias, since doskey::resolve() won't expand it as a doskey alias.
            uint32 command_char_offset = tmp[0].offset;
            if (tmp[0].quoted)
                command_char_offset--;
            if (command_char_offset == 1 && line_buffer[0] == ' ')
                command_char_offset--;
            else if (command_char_offset >= 2 &&
                     line_buffer[command_char_offset - 1] == ' ' &&
                     line_buffer[command_char_offset - 2] == ' ')
                command_char_offset--;

            m_words_storage.emplace_back(std::move(tmp));
            assert(tmp.empty());
            tmp.reserve(words.size());

            // The !tmp.empty() check effectively discarded command ranges with
            // no words.  Now it's still required for backward compatibility.
            assert(command_iter != commands.end());
            while (command_iter->offset + command_iter->length < command_char_offset)
            {
                ++command_iter;
                assert(command_iter != commands.end());
            }

            const bool limit_cursor = (mode == collect_words_mode::stop_at_cursor);
            const uint32 words_limit = limit_cursor ? line_cursor : line_length;

            m_linestates.emplace_back(std::move(line_state(
                line_buffer,
                line_length,
                line_cursor,
                words_limit,
                command_char_offset,
                command_iter->offset,
                command_iter->length,
                m_words_storage.back()
            )));
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
void command_line_states::set(const line_buffer& buffer,
                              const std::vector<word>& words,
                              collect_words_mode mode,
                              const std::vector<command>& commands)
{
    set(buffer.get_buffer(), buffer.get_length(), buffer.get_cursor(), words, mode, commands);
}

//------------------------------------------------------------------------------
uint32 command_line_states::break_end_word(uint32 truncate, uint32 keep)
{
#ifdef DEBUG
    assert(!m_broke_end_word);
    m_broke_end_word = true;
#endif

    word* end_word = const_cast<word*>(&m_linestates.back().get_words().back());
    if (truncate)
    {
        truncate = min<uint32>(truncate, end_word->length);

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

    keep = min<uint32>(keep, end_word->length);
    end_word->length = keep;
    return end_word->offset;
}

//------------------------------------------------------------------------------
void command_line_states::ensure_cursorpos_word_for_hinter()
{
    // Add an empty word when the cursor is past the last word.
    auto& line_state = m_linestates.back();
    const word* const end_word = const_cast<word*>(&line_state.get_words().back());
    const uint32 nextposafterendword = end_word->offset + end_word->length;
    const uint32 cursorpos = line_state.get_cursor();
    if ((cursorpos > nextposafterendword) ||
        (end_word->length && cursorpos == nextposafterendword && strpbrk(line_state.get_line() + nextposafterendword - 1, ":=")))
    {
        word empty_word;
        empty_word.offset = cursorpos;
        empty_word.length = 0;
        empty_word.command_word = false;
        empty_word.is_alias = false;
        empty_word.is_redir_arg = false;
        empty_word.quoted = false;
        empty_word.delim = str_token::invalid_delim;

        std::vector<word>* words = const_cast<std::vector<word>*>(&m_words_storage.back());
        words->push_back(empty_word);
    }
}

//------------------------------------------------------------------------------
void command_line_states::clear()
{
    clear_internal();

    m_words_storage.emplace_back();
    m_linestates.emplace_back(std::move(line_state(nullptr, 0, 0, 0, 0, 0, 0, m_words_storage[0])));
}

//------------------------------------------------------------------------------
void command_line_states::clear_internal()
{
    m_words_storage.clear();
    m_linestates.clear();
#ifdef DEBUG
    m_broke_end_word = false;
#endif
}

//------------------------------------------------------------------------------
const line_states& command_line_states::get_linestates(const char* buffer, uint32 len) const
{
    assert(m_linestates.size());
    const auto& front = m_linestates.front();
    if (buffer != front.get_line() || len != front.get_length())
    {
        assert(false);
        static line_states* s_none = nullptr;
        if (!s_none)
        {
            dbg_ignore_scope(snapshot, "globals; get_linestate");
            std::vector<word>* wv = new std::vector<word>;
            s_none = new line_states;
            s_none->push_back(std::move(line_state(nullptr, 0, 0, 0, 0, 0, 0, *wv)));
        }
        return *s_none;
    }
    return m_linestates;
}

//------------------------------------------------------------------------------
const line_states& command_line_states::get_linestates(const line_buffer& buffer) const
{
    return get_linestates(buffer.get_buffer(), buffer.get_length());
}

//------------------------------------------------------------------------------
const line_state& command_line_states::get_linestate(const char* buffer, uint32 len) const
{
    assert(m_linestates.size());
    const auto& back = m_linestates.back();
    if (buffer != back.get_line() || len != back.get_length())
    {
        assert(false);
        static line_state* s_none = nullptr;
        if (!s_none)
        {
            dbg_ignore_scope(snapshot, "globals; get_linestate");
            std::vector<word>* wv = new std::vector<word>;
            s_none = new line_state(nullptr, 0, 0, 0, 0, 0, 0, *wv);
        }
        return *s_none;
    }
    return back;
}

//------------------------------------------------------------------------------
const line_state& command_line_states::get_linestate(const line_buffer& buffer) const
{
    return get_linestate(buffer.get_buffer(), buffer.get_length());
}
