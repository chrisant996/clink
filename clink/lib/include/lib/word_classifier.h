// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class word_classifications;

#include <vector>

//------------------------------------------------------------------------------
class word_classifier
{
public:
    virtual void    classify(const std::vector<line_state>& commands, word_classifications& classifications) = 0;
};
