// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class line_buffer
{
public:
    virtual const char* get_buffer() const = 0;
    virtual int         get_cursor() const = 0;
    virtual int         set_cursor(unsigned int pos) = 0;
    virtual bool        insert(const char* text) = 0;
    virtual void        remove(unsigned int from, unsigned int to) = 0;
};
