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
    const array_base<word>& words;
    const char*             line;

    unsigned int            get_word_count() const;
    bool                    get_word(unsigned int index, str_base& out) const;
    bool                    get_end_word(str_base& out) const;
};

//------------------------------------------------------------------------------
inline unsigned int line_state::get_word_count() const
{
    return words.size();
}

//------------------------------------------------------------------------------
inline bool line_state::get_word(unsigned int index, str_base& out) const
{
    const word* word = words[index];
    if (word == nullptr)
        return false;

    return out.concat(line + word->offset, word->length);
}

//------------------------------------------------------------------------------
inline bool line_state::get_end_word(str_base& out) const
{
    int n = get_word_count();
    return (n ? get_word(n - 1, out) : false);
}
