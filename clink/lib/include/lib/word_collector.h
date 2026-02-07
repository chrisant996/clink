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
    enum : uint8 { invalid_delim = 0xff };
                        word_token(char c, bool arg=false) : delim(c), redir_arg(arg) {}
    explicit            operator bool () const { return (delim != invalid_delim); }
    uint8               delim;          // Preceding delimiter.
    bool                redir_arg;      // Word is the argument of a redirection symbol.
};

//------------------------------------------------------------------------------
class collector_tokeniser
{
public:
    virtual void start(const str_iter& iter, const char* quote_pair, bool at_beginning=true) = 0;
    virtual word_token next(uint32& offset, uint32& length) = 0;
    virtual bool has_deprecated_argmatcher(const char* command) { return false; }
};

//------------------------------------------------------------------------------
struct command
{
    uint32              offset;
    uint32              length;
    bool                is_alias_allowed;
};

//------------------------------------------------------------------------------
typedef std::vector<command> commands;

//------------------------------------------------------------------------------
class word_collector
{
public:
    word_collector(collector_tokeniser* command_tokeniser=nullptr, collector_tokeniser* word_tokeniser=nullptr, const char* quote_pair=nullptr);
    ~word_collector();

    void init_alias_cache();

    uint32 collect_words(const char* buffer, uint32 length, uint32 cursor,
                         words& words, collect_words_mode mode,
                         commands* commands) const;
    uint32 collect_words(const line_buffer& buffer,
                         words& words, collect_words_mode mode,
                         commands* commands) const;

private:
    char get_opening_quote() const;
    char get_closing_quote() const;
    void find_command_bounds(const char* buffer, uint32 length, uint32 cursor,
                             commands& commands, bool stop_at_cursor) const;
    bool get_alias(const char* name, str_base& out) const;
    bool is_alias_allowed(const char* buffer, uint32 offset) const;

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

    void start(const str_iter& iter, const char* quote_pair, bool at_beginning=true) override;
    word_token next(uint32& offset, uint32& length) override;

private:
    const char* m_delims;
    const char* m_start = nullptr;
    str_tokeniser* m_tokeniser = nullptr;
};

//------------------------------------------------------------------------------
class command_line_states
{
public:
    command_line_states() { clear(); }
    void set(const char* line_buffer, uint32 line_length, uint32 line_cursor, const words& words, collect_words_mode mode, const commands& commands);
    void set(const line_buffer& buffer, const words& words, collect_words_mode mode, const commands& commands);
    uint32 break_end_word(uint32 truncate, uint32 keep, bool discard);
    void split_for_hinter();
    void clear();
    const line_states& get_linestates(const char* buffer, uint32 len) const;
    const line_states& get_linestates(const line_buffer& buffer) const;
    const line_state& get_linestate(const char* buffer, uint32 len) const;
    const line_state& get_linestate(const line_buffer& buffer) const;
private:
    void clear_internal();
    std::vector<words> m_words_storage;
    line_states m_linestates;
#ifdef DEBUG
    bool m_broke_end_word;
#endif
};
