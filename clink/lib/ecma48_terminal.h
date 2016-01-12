// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal.h"

#include <core/ecma48_iter.h>

//------------------------------------------------------------------------------
class ecma48_terminal
    : public terminal
{
public:
                    ecma48_terminal();
    virtual         ~ecma48_terminal();
    virtual int     read() override;
    virtual void    write(const char* chars, int length) override;
    virtual void    flush() override;

private:
    void            write_csi(const ecma48_csi& csi);
    void            write_c0(int c0);
    void            write_impl(const char* chars, int length);
    void            check_sgr_support();
    ecma48_state    m_state;
    bool            m_enable_sgr;
};
