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
    virtual void        override_handle() override;
    virtual void        write(const char* chars, int32 length) override;
    virtual void        flush() override;
    virtual int32       get_columns() const override;
    virtual int32       get_rows() const override;
    virtual bool        get_line_text(int32 line, str_base& out) const override;
    virtual int32       is_line_default_color(int32 line) const override;
    virtual int32       line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const override;
    virtual int32       find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const override;

    static void         init_termcap_intercept();
    bool                do_termcap_intercept(const char* chars);
    void                visible_bell();

private:
    void                write_c1(const ecma48_code& code);
    void                write_c0(int32 c0);
    void                write_icf(const ecma48_code& code);
    void                set_attributes(const ecma48_code::csi_base& csi);
    void                erase_in_display(const ecma48_code::csi_base& csi);
    void                erase_in_line(const ecma48_code::csi_base& csi);
    void                set_horiz_cursor(const ecma48_code::csi_base& csi);
    void                set_cursor(const ecma48_code::csi_base& csi);
    void                save_cursor();
    void                restore_cursor();
    void                insert_chars(const ecma48_code::csi_base& csi);
    void                delete_chars(const ecma48_code::csi_base& csi);
    void                set_private_mode(const ecma48_code::csi_base& csi);
    void                reset_private_mode(const ecma48_code::csi_base& csi);
    int32               build_pending(char c);
    void                reset_pending();
    ecma48_state        m_state;
    screen_buffer&      m_screen;
    int32               m_ax;
    int32               m_encode_length;
    int32               m_pending = 0;
    char                m_buffer[4];
};
