// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;
template <typename T> class array;

//------------------------------------------------------------------------------
struct word
{
    unsigned int        offset : 14;
    unsigned int        length : 10;
    unsigned int        quoted : 1;
    unsigned int        delim  : 7;
};

//------------------------------------------------------------------------------
class line_state
{
public:
                        line_state(const array<word>& words, const char* line);
    const array<word>&  get_words() const;
    unsigned int        get_word_count() const;
    bool                get_word(unsigned int index, str_base& out) const;
    bool                get_end_word(str_base& out) const;

private:
    const array<word>&  m_words;
    const char*         m_line;
};
