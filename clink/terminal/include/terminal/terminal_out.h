// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "attributes.h"

class str_base;

//------------------------------------------------------------------------------
class terminal_out
{
public:
    virtual                 ~terminal_out() = default;
    virtual void            open() = 0;     // Not strictly required; begin() should implicitly open() if necessary.
    virtual void            begin() = 0;
    virtual void            end() = 0;
    virtual void            close() = 0;    // Should be not strictly required.
    virtual void            write(const char* chars, int length) = 0;
    template <int S> void   write(const char (&chars)[S]);
    virtual bool            get_line_text(int line, str_base& out) const = 0;
    virtual void            flush() = 0;
    virtual int             get_columns() const = 0;
    virtual int             get_rows() const = 0;
    virtual int             is_line_default_color(int line) const = 0;
    virtual int             line_has_color(int line, const BYTE* attrs, int num_attrs, BYTE mask=0xff) const = 0;
};

//------------------------------------------------------------------------------
template <int S> void terminal_out::write(const char (&chars)[S])
{
    write(chars, S - 1);
}
