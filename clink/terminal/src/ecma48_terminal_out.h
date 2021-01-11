// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "ecma48_iter.h"
#include "terminal_out.h"

class screen_buffer;
class str_base;

//------------------------------------------------------------------------------
class ecma48_terminal_out
    : public terminal_out
{
public:
                        ecma48_terminal_out(screen_buffer& screen);
    virtual void        open() override;
    virtual void        begin() override;
    virtual void        end() override;
    virtual void        close() override;
    virtual void        write(const char* chars, int length) override;
    virtual void        flush() override;
    virtual int         get_columns() const override;
    virtual int         get_rows() const override;
    virtual bool        get_line_text(int line, str_base& out) const override;
    virtual int         is_line_default_color(int line) const override;

private:
    void                write_c1(const ecma48_code& code);
    void                write_c0(int c0);
    void                set_attributes(const ecma48_code::csi_base& csi);
    void                erase_in_display(const ecma48_code::csi_base& csi);
    void                erase_in_line(const ecma48_code::csi_base& csi);
    void                set_cursor(const ecma48_code::csi_base& csi);
    void                insert_chars(const ecma48_code::csi_base& csi);
    void                delete_chars(const ecma48_code::csi_base& csi);
    void                set_private_mode(const ecma48_code::csi_base& csi);
    void                reset_private_mode(const ecma48_code::csi_base& csi);
    int                 build_pending(char c);
    void                reset_pending();
    ecma48_state        m_state;
    screen_buffer&      m_screen;
    int                 m_ax;
    int                 m_encode_length;
    int                 m_pending = 0;
    char                m_buffer[4];
};
