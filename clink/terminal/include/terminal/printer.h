// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "attributes.h"

class terminal_out;
class str_base;
enum find_line_mode : int32;

//------------------------------------------------------------------------------
void set_scrolled_screen_buffer();

//------------------------------------------------------------------------------
class printer
{
public:
                            printer(terminal_out& terminal);
    void                    reset();
    void                    print(const char* data, int32 bytes);
    void                    print(const char* attr, const char* data, int32 bytes);
    void                    print(const attributes attr, const char* data, int32 bytes);
    template <int32 S> void print(const char (&data)[S]);
    template <int32 S> void print(const char* attr, const char (&data)[S]);
    template <int32 S> void print(const attributes attr, const char (&data)[S]);
    uint32                  get_columns() const;
    uint32                  get_rows() const;
    uint32                  get_top() const;
    bool                    get_cursor(int16& x, int16& y) const;
    bool                    get_line_text(int32 line, str_base& out) const;
    int32                   is_line_default_color(int32 line) const;
    int32                   line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const;
    int32                   find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const;
    attributes              set_attributes(const attributes attr);
    attributes              get_attributes() const;

private:
    void                    flush_attributes();
    terminal_out&           m_terminal;
    attributes              m_set_attr;
    attributes              m_next_attr;
    bool                    m_nodiff;
};

//------------------------------------------------------------------------------
template <int32 S> void printer::print(const char (&data)[S])
{
    print(data, S - 1); // Don't include nul terminator.
}

//------------------------------------------------------------------------------
template <int32 S> void printer::print(const char* attr, const char (&data)[S])
{
    print(attr, data, S - 1); // Don't include nul terminator.
}

//------------------------------------------------------------------------------
template <int32 S> void printer::print(const attributes attr, const char (&data)[S])
{
    print(attr, data, S - 1); // Don't include nul terminator.
}
