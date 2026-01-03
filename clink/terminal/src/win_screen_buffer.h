// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "screen_buffer.h"

class str_base;
enum find_line_mode : int32;

//------------------------------------------------------------------------------
class win_screen_buffer
    : public screen_buffer
{
public:
    virtual         ~win_screen_buffer() override;
    virtual void    open() override;
    virtual void    begin() override;
    virtual void    end() override;
    virtual void    close() override;
    virtual void    write(const char* data, int32 length) override;
    virtual void    flush() override;
    virtual int32   get_columns() const override;
    virtual int32   get_rows() const override;
    virtual int32   get_top() const override;
    virtual bool    get_cursor(int16& x, int16& y) const override;
    virtual bool    get_line_text(int32 line, str_base& out) const override;
    virtual bool    has_native_vt_processing() const override;
    virtual void    clear(clear_type type) override;
    virtual void    clear_line(clear_type type) override;
    virtual void    set_horiz_cursor(int32 column) override;
    virtual void    set_cursor(int32 column, int32 row) override;
    virtual void    move_cursor(int32 dx, int32 dy) override;
    virtual void    save_cursor() override;
    virtual void    restore_cursor() override;
    virtual void    insert_chars(int32 count) override;
    virtual void    delete_chars(int32 count) override;
    virtual void    set_attributes(const attributes attr) override;
    virtual bool    find_best_palette_match(attributes& attr) const override;
    virtual int32   is_line_default_color(int32 line) const override;
    virtual int32   line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const override;
    virtual int32   find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const override;

    virtual void    override_handle() override;

private:
    bool            ensure_chars_buffer(int32 width) const;
    bool            ensure_attrs_buffer(int32 width) const;

    enum : unsigned short
    {
        attr_mask_fg        = 0x000f,
        attr_mask_bg        = 0x00f0,
        attr_mask_bold      = 0x0008,
        attr_mask_underline = 0x8000,
        attr_mask_all       = attr_mask_fg|attr_mask_bg|attr_mask_underline,
    };

    void*           m_handle = nullptr;
    DWORD           m_prev_mode = 0;
    uint16          m_default_attr = 0x07;
    uint16          m_ready = 0;
    bool            m_bold = false;
    bool            m_reverse = false;
    char            m_native_vt = -1;

    mutable WORD*   m_attrs = nullptr;
    mutable SHORT   m_attrs_capacity = 0;

    mutable WCHAR*  m_chars = nullptr;
    mutable SHORT   m_chars_capacity = 0;

    COORD           m_saved_cursor = { -1, -1 };
};
