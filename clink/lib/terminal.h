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

private:
};

//------------------------------------------------------------------------------
inline terminal::~terminal()
{
}
