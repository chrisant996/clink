// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal_out.h"

//------------------------------------------------------------------------------
class win_terminal_out
    : public terminal_out
{
public:
    virtual void    begin() override;
    virtual void    end() override;
    virtual void    write(const char* chars, int length) override;
    virtual void    flush() override;
    virtual int     get_columns() const override;
    virtual int     get_rows() const override;

private:
    void*           m_stdout = nullptr;
    unsigned long   m_prev_mode = 0;
    unsigned short  m_default_attr = 0x07;
};
