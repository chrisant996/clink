// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "screen_buffer.h"

//------------------------------------------------------------------------------
class win_screen_buffer
    : public screen_buffer
{
public:
    virtual void    begin() override;
    virtual void    end() override;
    virtual void    write(const char* data, int length) override;
    virtual void    flush() override;
    virtual int     get_columns() const override;
    virtual int     get_rows() const override;
    virtual void    clear(clear_type type) override;
    virtual void    clear_line(clear_type type) override;
    virtual void    set_cursor(int column, int row) override;
    virtual void    move_cursor(int dx, int dy) override;
    virtual void    insert_chars(int count) override;
    virtual void    delete_chars(int count) override;
    virtual void    set_attributes(const attributes attr) override;

private:
    enum : unsigned short
    {
        attr_mask_fg        = 0x000f,
        attr_mask_bg        = 0x00f0,
        attr_mask_bold      = 0x0008,
        attr_mask_underline = 0x8000,
        attr_mask_all       = attr_mask_fg|attr_mask_bg|attr_mask_underline,
    };

    void*           m_handle = nullptr;
    unsigned long   m_prev_mode = 0;
    unsigned short  m_default_attr = 0x07;
    bool            m_bold = false;
};
