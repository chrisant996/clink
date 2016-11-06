// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class match_builder;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual bool    generate(const line_state& line, match_builder& builder) = 0;
    virtual int     get_prefix_length(const char* start, int length) const = 0;

private:
};

match_generator& file_match_generator();
