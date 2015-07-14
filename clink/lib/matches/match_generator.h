// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "match_result.h"

class line_state;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual                 ~match_generator() = 0 {}
    virtual match_result    generate(const line_state& line) = 0;

private:
};
