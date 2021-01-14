// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal_out.h"

//------------------------------------------------------------------------------
class win_terminal_out
    : public terminal_out
{
public:
    virtual         ~win_terminal_out() override;
    virtual void    begin() override;
    virtual void    end() override;
    virtual void    write(const char* chars, int length) override;
    virtual void    flush() override;
    virtual int     get_columns() const override;
    virtual int     get_rows() const override;
    virtual bool    get_line_text(int line, str_base& out) const override;
    virtual int     is_line_default_color(int line) const override;
    virtual int     line_has_color(int line, const BYTE* attrs, int num_attrs, BYTE mask=0xff) const override;

private:
    void*           m_stdout = nullptr;
    unsigned long   m_prev_mode = 0;
    unsigned short  m_default_attr = 0x07;

    mutable WORD*   m_attrs = nullptr;
    mutable SHORT   m_attrs_capacity = 0;

    mutable WCHAR*  m_chars = nullptr;
    mutable SHORT   m_chars_capacity = 0;
};
