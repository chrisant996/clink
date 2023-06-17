// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "attributes.h"

class terminal_out;
class str_base;
enum find_line_mode : int;

//------------------------------------------------------------------------------
void set_scrolled_screen_buffer();

//------------------------------------------------------------------------------
class printer
{
public:
                            printer(terminal_out& terminal);
    void                    reset();
    void                    print(const char* data, int bytes);
    void                    print(const char* attr, const char* data, int bytes);
    void                    print(const attributes attr, const char* data, int bytes);
    template <int S> void   print(const char (&data)[S]);
    template <int S> void   print(const char* attr, const char (&data)[S]);
    template <int S> void   print(const attributes attr, const char (&data)[S]);
    unsigned int            get_columns() const;
    unsigned int            get_rows() const;
    bool                    get_line_text(int line, str_base& out) const;
    int                     is_line_default_color(int line) const;
    int                     line_has_color(int line, const BYTE* attrs, int num_attrs, BYTE mask=0xff) const;
    int                     find_line(int starting_line, int distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int num_attrs=0, BYTE mask=0xff) const;
    attributes              set_attributes(const attributes attr);
    attributes              get_attributes() const;

private: /* TODO: unimplemented API */
    typedef unsigned int    cursor_state;
    void                    insert(int count); // -count == delete characters.
    void                    move_cursor(int dc, int dr);
    void                    set_cursor(cursor_state state);
    cursor_state            get_cursor() const;

private:
    void                    flush_attributes();
    terminal_out&           m_terminal;
    attributes              m_set_attr;
    attributes              m_next_attr;
    bool                    m_nodiff;
};

//------------------------------------------------------------------------------
template <int S> void printer::print(const char (&data)[S])
{
    print(data, S - 1); // Don't include nul terminator.
}

//------------------------------------------------------------------------------
template <int S> void printer::print(const char* attr, const char (&data)[S])
{
    print(attr, data, S - 1); // Don't include nul terminator.
}

//------------------------------------------------------------------------------
template <int S> void printer::print(const attributes attr, const char (&data)[S])
{
    print(attr, data, S - 1); // Don't include nul terminator.
}
