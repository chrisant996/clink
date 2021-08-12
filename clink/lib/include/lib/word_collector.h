// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_state.h"

#include <core/str_iter.h>
#include <core/str_tokeniser.h> // for str_token

#include <vector>

class line_buffer;
class collector_tokeniser;

//------------------------------------------------------------------------------
enum class collect_words_mode { stop_at_cursor, display_filter, whole_command };

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

    unsigned int collect_words(const char* buffer, unsigned int length, unsigned int cursor,
                               std::vector<word>& words, collect_words_mode mode) const;
    unsigned int collect_words(const line_buffer& buffer,
                               std::vector<word>& words, collect_words_mode mode) const;

private:
    char get_opening_quote() const;
    char get_closing_quote() const;
    void find_command_bounds(const char* buffer, unsigned int length, unsigned int cursor,
                             std::vector<command>& commands, bool stop_at_cursor) const;

private:
    collector_tokeniser* const m_command_tokeniser;
    collector_tokeniser* m_word_tokeniser;
    const char* const m_quote_pair;
    bool m_delete_word_tokeniser = false;
};

//------------------------------------------------------------------------------
class collector_tokeniser
{
public:
    virtual void start(const str_iter& iter, const char* quote_pair) = 0;
    virtual str_token next(unsigned int& offset, unsigned int& length) = 0;
};

//------------------------------------------------------------------------------
class simple_word_tokeniser : public collector_tokeniser
{
public:
    simple_word_tokeniser(const char* delims = " \t");
    ~simple_word_tokeniser();

    void start(const str_iter& iter, const char* quote_pair) override;
    str_token next(unsigned int& offset, unsigned int& length) override;

private:
    const char* m_delims;
    const char* m_start = nullptr;
    str_tokeniser* m_tokeniser = nullptr;
};
