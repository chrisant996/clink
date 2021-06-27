// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_state.h"

#include <vector>

class line_buffer;

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
    word_collector(const char* command_delims, const char* word_delims, const char* quote_pair);

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
    const char* const m_command_delims;
    const char* const m_word_delims;
    const char* const m_quote_pair;
};
