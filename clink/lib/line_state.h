// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>
#include <core/str.h>

//------------------------------------------------------------------------------
struct word
{
    unsigned short          offset;
    unsigned short          length;
};

//------------------------------------------------------------------------------
class line_state
{
public:
                        line_state(const array<word>& words, const char* line);
    unsigned int        get_word_count() const;
    bool                get_word(unsigned int index, str_base& out) const;
    bool                get_end_word(str_base& out) const;
    const array<word>&  get_words() const;

private:
    const array<word>&  m_words;
    const char*         m_line;
};

//------------------------------------------------------------------------------
inline line_state::line_state(const array<word>& words, const char* line)
: m_words(words)
, m_line(line)
{
}

//------------------------------------------------------------------------------
inline unsigned int line_state::get_word_count() const
{
    return m_words.size();
}

//------------------------------------------------------------------------------
inline bool line_state::get_word(unsigned int index, str_base& out) const
{
    const word* word = m_words[index];
    if (word == nullptr)
        return false;

    return out.concat(m_line + word->offset, word->length);
}

//------------------------------------------------------------------------------
inline bool line_state::get_end_word(str_base& out) const
{
    int n = get_word_count();
    return (n ? get_word(n - 1, out) : false);
}

//------------------------------------------------------------------------------
inline const array<word>& line_state::get_words() const
{
    return m_words;
}
