// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

//------------------------------------------------------------------------------
// MODE4
class line_state
{
public:
    const char*     word;
    const char*     line;
    int             start;
    int             end;
    int             cursor;
};
// MODE4

//------------------------------------------------------------------------------
struct word
{
    unsigned short offset;
    unsigned short length;
    unsigned short partial;
    unsigned short delim;
};

//------------------------------------------------------------------------------
struct line_state_2
{
    fixed_array<word, 128>& words;
    const char*             line;
    int                     cursor;
};
