// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "word_collector.h"

//------------------------------------------------------------------------------
enum tokeniser_state;

//------------------------------------------------------------------------------
enum state_flag
{
    flag_none           = 0x00,
    flag_rem            = 0x01,
};
DEFINE_ENUM_FLAG_OPERATORS(state_flag);

//------------------------------------------------------------------------------
class cmd_state
{
public:
    cmd_state(bool only_rem=false) : m_only_rem(only_rem) {}
    void clear();
    void next_word();
    bool test(int c, tokeniser_state new_state);
    void cancel() { m_failed = true; }
private:
    str<16> m_word;
    bool m_first = false;
    bool m_failed = true;
    bool m_match = false;
    state_flag m_match_flag = flag_none;
    const bool m_only_rem;
    static const char* const c_delimit;
};

//------------------------------------------------------------------------------
class cmd_tokeniser_impl : public collector_tokeniser
{
public:
    cmd_tokeniser_impl();
    ~cmd_tokeniser_impl();
    void begin_line();
    void start(const str_iter& iter, const char* quote_pair) override;
protected:
    char get_opening_quote() const;
    char get_closing_quote() const;
protected:
    str_iter m_iter;
    const char* m_start;
    const char* m_quote_pair;
    alias_cache* m_alias_cache = nullptr;
    bool m_next_redir_arg;
};

//------------------------------------------------------------------------------
class cmd_command_tokeniser : public cmd_tokeniser_impl
{
public:
    word_token next(unsigned int& offset, unsigned int& length) override;
    bool has_deprecated_argmatcher(char const* command) override;
};

//------------------------------------------------------------------------------
class cmd_word_tokeniser : public cmd_tokeniser_impl
{
    typedef cmd_tokeniser_impl base;
public:
    void start(const str_iter& iter, const char* quote_pair) override;
    word_token next(unsigned int& offset, unsigned int& length) override;
private:
    cmd_state m_cmd_state;
};

//------------------------------------------------------------------------------
bool is_cmd_command(const char* word, state_flag* flag=nullptr);
int skip_leading_parens(str_iter& iter, bool& first, alias_cache* alias_cache=nullptr);
unsigned int trim_trailing_parens(const char* start, unsigned int offset, unsigned int length, int parens);
