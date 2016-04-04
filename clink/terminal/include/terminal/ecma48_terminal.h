// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal.h"
#include "ecma48_iter.h"

#include <Windows.h>

//------------------------------------------------------------------------------
class xterm_input
{
public:
                    xterm_input();
    int             read();

private:
    int             read_console();
    void            push(char value);
    void            push(int value) { push((char)(unsigned char)value); /* MODE4 */ }
    int             pop();
    int             m_buffer_head;
    int             m_buffer_count;
    char            m_buffer[8]; // must be power of two.
};



//------------------------------------------------------------------------------
class ecma48_terminal
    : public terminal
{
public:
                    ecma48_terminal();
    virtual         ~ecma48_terminal();
    virtual void    begin() override;
    virtual void    end() override;
    virtual int     read() override;
    virtual void    write(const char* chars, int length) override;
    virtual void    flush() override;
    virtual int     get_columns() const override;
    virtual int     get_rows() const override;

private:
    void            write_csi(const ecma48_code& code);
    void            write_sgr(const ecma48_csi& csi);
    void            write_c0(int c0);
    void            write_impl(const char* chars, int length);
    void            check_sgr_support();
    HANDLE          m_handle;
    xterm_input     m_xterm_input;
    ecma48_state    m_state;
    int             m_default_attr;
    int             m_attr;
    bool            m_enable_sgr;
};
