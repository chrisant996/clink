// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class terminal_in
{
public:
    enum {
        input_none              = 0x80000000,
        input_terminal_resize,
    };

    virtual void    select() = 0;
    virtual int     read() = 0;
};

//------------------------------------------------------------------------------
class terminal_out
{
public:
    virtual void    write(const char* chars, int length) = 0;
    virtual void    flush() = 0;
    virtual int     get_columns() const = 0;
    virtual int     get_rows() const = 0;
};

//------------------------------------------------------------------------------
class terminal
    : public terminal_in
    , public terminal_out
{
public:
    virtual         ~terminal() = default;
    virtual void    begin() = 0;
    virtual void    end() = 0;

private:
};

//------------------------------------------------------------------------------
struct auto_flush
{
                    auto_flush(terminal_out& term) : m_term(term) {}
                    ~auto_flush() { m_term.flush(); }
    terminal_out&   m_term;
};
