// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal.h"

//------------------------------------------------------------------------------
class ecma48_terminal
    : public terminal
{
public:
                    ecma48_terminal();
    virtual         ~ecma48_terminal();
    virtual int     read() override;
    virtual void    write(const wchar_t* chars, int char_count) override;
    virtual void    flush() override;

private:
    void            check_sgr_support();
    bool            m_enable_sgr;
};
