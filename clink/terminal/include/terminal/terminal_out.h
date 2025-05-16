// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "attributes.h"

class str_base;

//------------------------------------------------------------------------------
enum find_line_mode : int32;

//------------------------------------------------------------------------------
class terminal_out
{
public:
    virtual                 ~terminal_out() = default;
    virtual void            open() = 0;     // Not strictly required; begin() should implicitly open() if necessary.
    virtual void            begin() = 0;
    virtual void            end() = 0;
    virtual void            close() = 0;    // Should be not strictly required.
    virtual void            override_handle() {}
    virtual void            write(const char* chars, int32 length) = 0;
    template <int32 S> void write(const char (&chars)[S]);
    virtual bool            get_line_text(int32 line, str_base& out) const = 0;
    virtual void            flush() = 0;
    virtual int32           get_columns() const = 0;
    virtual int32           get_rows() const = 0;
    virtual int32           is_line_default_color(int32 line) const = 0;
    virtual int32           line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const = 0;
    virtual int32           find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const = 0;
};

//------------------------------------------------------------------------------
template <int32 S> void terminal_out::write(const char (&chars)[S])
{
    write(chars, S - 1);
}
