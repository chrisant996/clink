// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_state.h"

#include <core/str_iter.h>
#include <core/str_tokeniser.h>

#include <vector>

class line_buffer;
class collector_tokeniser;
class alias_cache;

//------------------------------------------------------------------------------
enum class collect_words_mode { stop_at_cursor, whole_command };

//------------------------------------------------------------------------------
class word_token
{
public:
    enum : unsigned char { invalid_delim = 0xff };
                        word_token(char c, bool arg=false) : delim(c), redir_arg(arg) {}
    explicit            operator bool () const { return (delim != invalid_delim); }
    unsigned char       delim;          // Preceding delimiter.
    bool                redir_arg;      // Word is the argument of a redirection symbol.
};

//------------------------------------------------------------------------------
class collector_tokeniser
{
public:
    virtual void start(const str_iter& iter, const char* quote_pair) = 0;
    virtual word_token next(unsigned int& offset, unsigned int& length) = 0;
    virtual bool has_deprecated_argmatcher(const char* command) { return false; }
};

//------------------------------------------------------------------------------
class word_collector
{
    struct command
    {
        unsigned int        offset;
        unsigned int        length;
    };

public:
    word_collector(collector_tokeniser* command_tokeniser=nullptr, collector_tokeniser* word_tokeniser=nullptr, const char* quote_pair=nullptr);
    ~word_collector();

    void init_alias_cache();

    unsigned int collect_words(const char* buffer, unsigned int length, unsigned int cursor,
                               std::vector<word>& words, collect_words_mode mode) const;
    unsigned int collect_words(const line_buffer& buffer,
                               std::vector<word>& words, collect_words_mode mode) const;

private:
    char get_opening_quote() const;
    char get_closing_quote() const;
    void find_command_bounds(const char* buffer, unsigned int length, unsigned int cursor,
                             std::vector<command>& commands, bool stop_at_cursor) const;
    bool get_alias(const char* name, str_base& out) const;

private:
    collector_tokeniser* const m_command_tokeniser;
    collector_tokeniser* m_word_tokeniser;
    alias_cache* m_alias_cache = nullptr;
    const char* const m_quote_pair;
    bool m_delete_word_tokeniser = false;
};

//------------------------------------------------------------------------------
class simple_word_tokeniser : public collector_tokeniser
{
public:
    simple_word_tokeniser(const char* delims = " \t");
    ~simple_word_tokeniser();

    void start(const str_iter& iter, const char* quote_pair) override;
    word_token next(unsigned int& offset, unsigned int& length) override;

private:
    const char* m_delims;
    const char* m_start = nullptr;
    str_tokeniser* m_tokeniser = nullptr;
};

//------------------------------------------------------------------------------
class commands
{
public:
    commands() { clear(); }
    void set(const char* line_buffer, unsigned int line_length, unsigned int line_cursor, const std::vector<word>& words);
    void set(const line_buffer& buffer, const std::vector<word>& words);
    unsigned int break_end_word(unsigned int truncate, unsigned int keep);
    void clear();
    const line_states& get_linestates(const char* buffer, unsigned int len) const;
    const line_states& get_linestates(const line_buffer& buffer) const;
    const line_state& get_linestate(const line_buffer& buffer) const;
private:
    void clear_internal();
    std::vector<std::vector<word>> m_words_storage;
    line_states m_linestates;
#ifdef DEBUG
    bool m_broke_end_word;
#endif
};
