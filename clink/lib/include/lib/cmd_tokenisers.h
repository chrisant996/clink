// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "word_collector.h"

//------------------------------------------------------------------------------
class cmd_tokeniser_impl : public collector_tokeniser
{
public:
    void start(const str_iter& iter, const char* quote_pair) override;
protected:
    char get_opening_quote() const;
    char get_closing_quote() const;
protected:
    str_iter m_iter;
    const char* m_start;
    const char* m_quote_pair;
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
public:
    word_token next(unsigned int& offset, unsigned int& length) override;
};
