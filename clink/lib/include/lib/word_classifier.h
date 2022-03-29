// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class line_states;
class word_classifications;

#include <vector>

//------------------------------------------------------------------------------
class word_classifier
{
public:
    virtual void    classify(const line_states& commands, word_classifications& classifications) = 0;
};
