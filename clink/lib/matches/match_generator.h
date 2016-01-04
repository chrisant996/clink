// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class matches;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual bool    generate(const line_state& line, matches& out) = 0;

private:
};
