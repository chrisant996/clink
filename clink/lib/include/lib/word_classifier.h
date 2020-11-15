// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

class line_state;

//------------------------------------------------------------------------------
enum class word_class : unsigned char
{
    other = 0,
    command,    // 'c'
    doskey,     // 'd'
    arg,        // 'a'
    flag,       // 'f'
    none,       // 'n'
    max
};

//------------------------------------------------------------------------------
typedef fixed_array<word_class, 72> word_classifications;

//------------------------------------------------------------------------------
class word_classifier
{
public:
    virtual void    classify(const line_state& line, word_classifications& classifications) const = 0;
};
