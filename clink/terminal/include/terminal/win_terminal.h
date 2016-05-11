// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal.h"
#include "ecma48_iter.h"

template <typename T> class array;

//------------------------------------------------------------------------------
class win_terminal_in
{
protected:
    void            begin();
    void            end();
    void            select();
    int             read();
    void            read_console();
    void            push(unsigned int value);
    unsigned char   pop();

private:
    void*           m_stdin = nullptr;
    unsigned long   m_prev_mode = 0;
    unsigned char   m_buffer_head = 0;
    unsigned char   m_buffer_count = 0;
    unsigned char   m_buffer[8]; // must be power of two.
};



//------------------------------------------------------------------------------
class win_terminal_out
{
protected:
    void            begin();
    void            end();
    void            write(const char* chars, int length);
    void            write(const wchar_t* chars, int length);
    void            flush();
    int             get_columns() const;
    int             get_rows() const;
    unsigned char   get_default_attr() const;
    unsigned char   get_attr() const;
    void            set_attr(unsigned char attr);

private:
    void*           m_stdout = nullptr;
    unsigned long   m_prev_mode = 0;
    unsigned char   m_default_attr = 0x07;
    unsigned char   m_attr = 0;
};


//------------------------------------------------------------------------------
class win_terminal
    : public terminal
    , public win_terminal_in
    , public win_terminal_out
{
public:
    virtual void    begin() override;
    virtual void    end() override;
    virtual void    select() override;
    virtual int     read() override;
    virtual void    write(const char* chars, int length) override;
    virtual void    flush() override;
    virtual int     get_columns() const override;
    virtual int     get_rows() const override;

private:
    void            write_c1(const ecma48_code& code);
    void            write_sgr(const array<int>& params);
    void            write_c0(int c0);
    void            check_c1_support();
    ecma48_state    m_state;
    bool            m_enable_c1 = true;
};
