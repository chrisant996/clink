// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state.h"

#include <core/array.h>
#include <core/str.h>

//------------------------------------------------------------------------------
line_state::line_state(const array<word>& words, const char* line)
: m_words(words)
, m_line(line)
{
}

//------------------------------------------------------------------------------
const array<word>& line_state::get_words() const
{
    return m_words;
}

//------------------------------------------------------------------------------
unsigned int line_state::get_word_count() const
{
    return m_words.size();
}

//------------------------------------------------------------------------------
bool line_state::get_word(unsigned int index, str_base& out) const
{
    const word* word = m_words[index];
    if (word == nullptr)
        return false;

    out.concat(m_line + word->offset, word->length);
    return true;
}

//------------------------------------------------------------------------------
bool line_state::get_end_word(str_base& out) const
{
    int n = get_word_count();
    return (n ? get_word(n - 1, out) : false);
}
