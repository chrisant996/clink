// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "attributes.h"

class str_base;
enum find_line_mode : int32;

//------------------------------------------------------------------------------
enum class ansi_handler : int32
{
    unknown,
    clink,
    ansicon,    // Use emulation with ANSICON due to compatibility problems.
    first_native,
    conemu = first_native,
    winterminal,
    wezterm,
    winconsolev2,
    winconsole,
    max
};
ansi_handler get_native_ansi_handler();
ansi_handler get_current_ansi_handler();

//------------------------------------------------------------------------------
class screen_buffer
{
public:
    enum clear_type
    {
        clear_type_before,
        clear_type_after,
        clear_type_all,
    };

    virtual         ~screen_buffer() = default;
    virtual void    open() = 0;
    virtual void    begin() = 0;
    virtual void    end() = 0;
    virtual void    close() = 0;
    virtual void    write(const char* data, int32 length) = 0;
    virtual void    flush() = 0;
    virtual int32   get_columns() const = 0;
    virtual int32   get_rows() const = 0;
    virtual bool    get_line_text(int32 line, str_base& out) const = 0;
    virtual bool    has_native_vt_processing() const = 0;
    virtual void    clear(clear_type type) = 0;
    virtual void    clear_line(clear_type type) = 0;
    virtual void    set_horiz_cursor(int32 column) = 0;
    virtual void    set_cursor(int32 column, int32 row) = 0;
    virtual void    move_cursor(int32 dx, int32 dy) = 0;
    virtual void    save_cursor() = 0;
    virtual void    restore_cursor() = 0;
    virtual void    insert_chars(int32 count) = 0;
    virtual void    delete_chars(int32 count) = 0;
    virtual void    set_attributes(const attributes attr) = 0;
    virtual bool    get_nearest_color(attributes& attr) const = 0;
    virtual int32   is_line_default_color(int32 line) const = 0;
    virtual int32   line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const = 0;
    virtual int32   find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const = 0;
};

//------------------------------------------------------------------------------
void set_console_title(const char* title);
