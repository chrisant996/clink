// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "attributes.h"

class terminal_out;

//------------------------------------------------------------------------------
class printer
{
public:
                            printer(terminal_out& terminal);
    void                    print(const char* data, int bytes);
    void                    print(const attributes attr, const char* data, int bytes);
    template <int S> void   print(const char (&data)[S]);
    template <int S> void   print(const attributes attr, const char (&data)[S]);
    unsigned int            get_columns() const;
    unsigned int            get_rows() const;
    attributes              set_attributes(const attributes attr);
    attributes              get_attributes() const;

private:
    void                    flush_attributes();
    terminal_out&           m_terminal;
    attributes              m_set_attr;
    attributes              m_next_attr;
};

//------------------------------------------------------------------------------
template <int S> void printer::print(const char (&data)[S])
{
    print(data, S);
}

//------------------------------------------------------------------------------
template <int S> void printer::print(const attributes attr, const char (&data)[S])
{
    print(attr, data, S);
}
