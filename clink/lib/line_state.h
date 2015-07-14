// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class line_state
{
public:
    const char*     word;
    const char*     line;
    int             start;
    int             end;
    int             cursor;
};
