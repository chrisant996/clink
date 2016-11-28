// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class terminal_out;

//------------------------------------------------------------------------------
class printer
{
public:
                            printer(terminal_out& terminal);
    void                    print(const char* data, int bytes);
    template <int S> void   print(const char (&data)[S]);
    unsigned int            get_columns() const;
    unsigned int            get_rows() const;

private:
    terminal_out&           m_terminal;
};

//------------------------------------------------------------------------------
template <int S> void printer::print(const char (&data)[S])
{
    print(data, S);
}
