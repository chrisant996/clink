// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class line_buffer
{
public:
    virtual                 ~line_buffer() = default;
    virtual const char*     get_buffer() const = 0;
    virtual unsigned int    get_cursor() const = 0;
    virtual unsigned int    set_cursor(unsigned int pos) = 0;
    virtual bool            insert(const char* text) = 0;
    virtual bool            remove(unsigned int from, unsigned int to) = 0;
    virtual void            draw() = 0;
    virtual void            redraw() = 0;
};
