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
class word_classifications : public fixed_array<word_class, 72>
{
public:
                    word_classifications() = default;
                    ~word_classifications() = default;
};

//------------------------------------------------------------------------------
class word_classifier
{
public:
    virtual void    classify(const line_state& line, word_classifications& classifications) const = 0;
};
