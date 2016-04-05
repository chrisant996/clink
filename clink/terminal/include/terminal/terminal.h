// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class terminal_in
{
public:
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
    virtual         ~terminal() {}
    virtual void    begin() = 0;
    virtual void    end() = 0;

private:
};
