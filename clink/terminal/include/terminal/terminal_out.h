// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class terminal_out
{
public:
    virtual void    begin() = 0;
    virtual void    end() = 0;
    virtual void    write(const char* chars, int length) = 0;
    virtual void    flush() = 0;
    virtual int     get_columns() const = 0;
    virtual int     get_rows() const = 0;
};

//------------------------------------------------------------------------------
struct auto_flush
{
                    auto_flush(terminal_out& term) : m_term(term) {}
                    ~auto_flush() { m_term.flush(); }
    terminal_out&   m_term;
};
