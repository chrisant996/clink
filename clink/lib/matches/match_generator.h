// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class matches_builder;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual void    generate(const line_state& line, matches_builder& builder) = 0;

private:
};
