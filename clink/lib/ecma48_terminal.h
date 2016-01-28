// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal.h"

#include <core/ecma48_iter.h>

//------------------------------------------------------------------------------
class xterm_input
{
public:
                    xterm_input();
    int             read();

private:
    int             read_console();
    void            push(int value);
    int             pop();
    int             m_buffer_head;
    int             m_buffer_count;
    int             m_buffer[8]; // must be power of two.
};



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
    virtual int     get_columns() const override;
    virtual int     get_rows() const override;

private:
    void            write_csi(const ecma48_csi& csi);
    void            write_c0(int c0);
    void            write_impl(const char* chars, int length);
    void            check_sgr_support();
    xterm_input     m_xterm_input;
    ecma48_state    m_state;
    bool            m_enable_sgr;
};
