// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches.h"

class line_state;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual void    generate(const line_state& line, matches& result) = 0;

private:
};
