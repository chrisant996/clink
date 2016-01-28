// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class terminal
{
public:
    virtual         ~terminal();
    virtual int     read() = 0;
    virtual void    write(const char* chars, int length) = 0;
    virtual void    flush() = 0;
    virtual int     get_columns() const = 0;
    virtual int     get_rows() const = 0;

private:
};

//------------------------------------------------------------------------------
inline terminal::~terminal()
{
}
