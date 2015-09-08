// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "match_result.h"

class line_state;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual void    generate(const line_state& line, match_result& result) = 0;

private:
};
