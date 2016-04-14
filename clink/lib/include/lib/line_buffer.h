// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class line_buffer
{
public:
    virtual const char* get_buffer() const = 0;
    virtual int         get_cursor_pos() const = 0;
};
