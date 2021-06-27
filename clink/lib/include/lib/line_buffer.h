// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

struct word;

//------------------------------------------------------------------------------
class line_buffer
{
public:
    virtual                 ~line_buffer() = default;
    virtual void            reset() = 0;
    virtual void            begin_line() = 0;
    virtual void            end_line() = 0;
    virtual const char*     get_buffer() const = 0;
    virtual unsigned int    get_length() const = 0;
    virtual unsigned int    get_cursor() const = 0;
    virtual unsigned int    set_cursor(unsigned int pos) = 0;
    virtual bool            insert(const char* text) = 0;
    virtual bool            remove(unsigned int from, unsigned int to) = 0;
    virtual void            begin_undo_group() = 0;
    virtual void            end_undo_group() = 0;
    virtual void            draw() = 0;
    virtual void            redraw() = 0;
    virtual void            set_need_draw() = 0;
};
