// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

// https://ecma-international.org/publications-and-standards/standards/ecma-48/

#include "pch.h"
#include "ecma48_terminal_out.h"
#include "ecma48_iter.h"
#include "screen_buffer.h"
#include "terminal.h"
#include "terminal_helpers.h"

#include <core/settings.h>

#include <assert.h>

//------------------------------------------------------------------------------
extern setting_bool g_adjust_cursor_style;
extern "C" char *tgetstr(const char* name, char** out);

//------------------------------------------------------------------------------
static uint8 s_rgb_cube[] = { 0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff };

//------------------------------------------------------------------------------
void set_console_title(const char* title)
{
    wstr<> out;
    to_utf16(out, title);
    SetConsoleTitleW(out.c_str());
}



//------------------------------------------------------------------------------
ecma48_terminal_out::ecma48_terminal_out(screen_buffer& screen)
: m_screen(screen)
{
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::override_handle()
{
    m_screen.override_handle();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::open()
{
    m_screen.open();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::begin()
{
    m_screen.begin();
    reset_pending();
    init_termcap_intercept();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::end()
{
    m_screen.end();
    reset_pending();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::close()
{
    m_screen.close();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::flush()
{
    m_screen.flush();
    reset_pending();
}

//------------------------------------------------------------------------------
int32 ecma48_terminal_out::get_columns() const
{
    return m_screen.get_columns();
}

//------------------------------------------------------------------------------
int32 ecma48_terminal_out::get_rows() const
{
    return m_screen.get_rows();
}

//------------------------------------------------------------------------------
bool ecma48_terminal_out::get_line_text(int32 line, str_base& out) const
{
    return m_screen.get_line_text(line, out);
}

//------------------------------------------------------------------------------
int32 ecma48_terminal_out::is_line_default_color(int32 line) const
{
    return m_screen.is_line_default_color(line);
}

//------------------------------------------------------------------------------
int32 ecma48_terminal_out::line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask) const
{
    return m_screen.line_has_color(line, attrs, num_attrs, mask);
}

//------------------------------------------------------------------------------
int32 ecma48_terminal_out::find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs, int32 num_attrs, BYTE mask) const
{
    return m_screen.find_line(starting_line, distance, text, mode, attrs, num_attrs, mask);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write_c1(const ecma48_code& code)
{
    if (code.get_code() == ecma48_code::c1_csi)
    {
        ecma48_code::csi<32> csi;
        code.decode_csi(csi);

        if (csi.private_use)
        {
            switch (csi.final)
            {
            case 'h': set_private_mode(csi);    break;
            case 'l': reset_private_mode(csi);  break;
            }
        }
        else
        {
            switch (csi.final)
            {
            case '@': insert_chars(csi);        break;
            case 'G': set_horiz_cursor(csi);    break;
            case 'H': set_cursor(csi);          break;
            case 'J': erase_in_display(csi);    break;
            case 'K': erase_in_line(csi);       break;
            case 'P': delete_chars(csi);        break;
            case 'm': set_attributes(csi);      break;
            case 's': save_cursor();            break;
            case 'u': restore_cursor();         break;

            case 'A': m_screen.move_cursor(0, -csi.get_param(0, 1)); break;
            case 'B': m_screen.move_cursor(0,  csi.get_param(0, 1)); break;
            case 'C': m_screen.move_cursor( csi.get_param(0, 1), 0); break;
            case 'D': m_screen.move_cursor(-csi.get_param(0, 1), 0); break;
            }
        }
    }
    else if (code.get_code() == ecma48_code::c1_osc)
    {
        ecma48_code::osc osc;
        if (code.decode_osc(osc))
        {
            switch (osc.command)
            {
            case '0':
            case '1':
            case '2':
                set_console_title(osc.param.c_str());
                break;

            case '9':
                if (osc.output.length())
                    write(osc.output.c_str(), osc.output.length());
                break;
            }
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write_c0(int32 c0)
{
    switch (c0)
    {
    case ecma48_code::c0_bel:
        MessageBeep(0xffffffff);
        break;

    case ecma48_code::c0_bs:
        m_screen.move_cursor(-1, 0);
        break;

    case ecma48_code::c0_cr:
        m_screen.move_cursor(INT_MIN, 0);
        break;

    case ecma48_code::c0_ht: // TODO: perhaps there should be a next_tab_stop() method?
    case ecma48_code::c0_lf: // TODO: shouldn't expect screen_buffer impl to react to '\n' characters.
        {
            char c = char(c0);
            m_screen.write(&c, 1);
            break;
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write_icf(const ecma48_code& code)
{
    if (code.get_code() == ecma48_code::icf_vb)
    {
        visible_bell();
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write(const char* chars, int32 length)
{
    if (length == 1 || (length < 0 && (chars[0] && !chars[1])))
    {
        // Readline sends one char at a time, but str_iter_impl doesn't support
        // utf8 conversion split across multiple calls.  So for now we'll buffer
        // a utf8 sequence here before letting ecma48_iter see it.
        if (!build_pending(*chars))
            return;
        chars = m_buffer;
        length = m_pending;
    }
    reset_pending();

    if (do_termcap_intercept(chars))
        return;

    if (m_screen.has_native_vt_processing())
    {
        m_screen.write(chars, length);
        return;
    }

    int32 need_next = (length == 1 || (chars[0] && !chars[1]));
    ecma48_iter iter(chars, m_state, length);
    while (const ecma48_code& code = iter.next())
    {
        switch (code.get_type())
        {
        case ecma48_code::type_chars:
            m_screen.write(code.get_pointer(), code.get_length());
            break;

        case ecma48_code::type_c0:
            write_c0(code.get_code());
            break;

        case ecma48_code::type_c1:
            write_c1(code);
            break;

        case ecma48_code::type_icf:
            write_icf(code);
            break;
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_attributes(const ecma48_code::csi_base& csi)
{
    reset_pending();

    // Empty parameters to 'CSI SGR' implies 0 (reset).
    if (csi.param_count == 0)
        return m_screen.set_attributes(attributes::defaults);

    // Process each code that is supported.
    attributes attr;
    for (int32 i = 0, n = csi.param_count; i < csi.param_count; ++i, --n)
    {
        uint32 param = csi.params[i];

        switch (param)
        {
        // Resets.
        case 0:     attr = attributes::defaults; break;
        case 49:    attr.reset_bg(); break;
        case 39:    attr.reset_fg(); break;

        // Bold.
        case 1:
        case 2:
        case 22:
            attr.set_bold(param == 1);
            break;

        // Underline.
        case 4:
        case 24:
            attr.set_underline(param == 4);
            break;

        // Foreground colors.
        case 30:    case 90:
        case 31:    case 91:
        case 32:    case 92:
        case 33:    case 93:
        case 34:    case 94:
        case 35:    case 95:
        case 36:    case 96:
        case 37:    case 97:
            param += (param >= 90) ? 14 : 2;
            attr.set_fg(param & 0x0f);
            break;

        // Background colors.
        case 40:    case 100:
        case 41:    case 101:
        case 42:    case 102:
        case 43:    case 103:
        case 44:    case 104:
        case 45:    case 105:
        case 46:    case 106:
        case 47:    case 107:
            param += (param >= 100) ? 4 : 8;
            attr.set_bg(param & 0x0f);
            break;

        // Reverse.
        case 7:
        case 27:
            attr.set_reverse(param == 7);
            break;

        // Xterm extended color support.
        case 38:
        case 48:
            if (n > 1)
            {
                i++;
                n--;
                bool is_fg = (param == 38);
                uint32 type = csi.params[i];
                if (type == 2)
                {
                    // RGB 24-bit color
                    if (n > 3)
                    {
                        if (is_fg)
                            attr.set_fg(csi.params[i + 1], csi.params[i + 2], csi.params[i + 3]);
                        else
                            attr.set_bg(csi.params[i + 1], csi.params[i + 2], csi.params[i + 3]);
                    }
                    i += 3;
                    n -= 3;
                }
                else if (type == 5)
                {
                    // XTerm256 color
                    if (n > 1)
                    {
                        uint8 idx = csi.params[i + 1];
                        if (idx < 16)
                        {
                            if (is_fg)
                                attr.set_fg(idx);
                            else
                                attr.set_bg(idx);
                        }
                        else if (idx >= 232)
                        {
                            uint8 gray = 0x08 + (int32(idx) - 232) * 10;
                            if (is_fg)
                                attr.set_fg(gray, gray, gray);
                            else
                                attr.set_bg(gray, gray, gray);
                        }
                        else
                        {
                            idx -= 16;
                            uint8 b = idx % 6;
                            idx /= 6;
                            uint8 g = idx % 6;
                            idx /= 6;
                            uint8 r = idx;
                            if (is_fg)
                                attr.set_fg(s_rgb_cube[r], s_rgb_cube[g], s_rgb_cube[b]);
                            else
                                attr.set_bg(s_rgb_cube[r], s_rgb_cube[g], s_rgb_cube[b]);
                        }
                    }
                    i++;
                    n--;
                }
            }
            break;
        }
    }

    m_screen.set_attributes(attr);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::erase_in_display(const ecma48_code::csi_base& csi)
{
    /* CSI ? Ps J : Erase in Display (DECSED).
            Ps = 0  -> Selective Erase Below (default).
            Ps = 1  -> Selective Erase Above.
            Ps = 2  -> Selective Erase All.
            Ps = 3  -> Selective Erase Saved Lines (xterm). */

    switch (csi.get_param(0))
    {
    case 0: m_screen.clear(screen_buffer::clear_type_after);    break;
    case 1: m_screen.clear(screen_buffer::clear_type_before);   break;
    case 2: m_screen.clear(screen_buffer::clear_type_all);      break;
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::erase_in_line(const ecma48_code::csi_base& csi)
{
    /* CSI Ps K : Erase in Line (EL).
            Ps = 0  -> Erase to Right (default).
            Ps = 1  -> Erase to Left.
            Ps = 2  -> Erase All. */

    switch (csi.get_param(0))
    {
    case 0: m_screen.clear_line(screen_buffer::clear_type_after);   break;
    case 1: m_screen.clear_line(screen_buffer::clear_type_before);  break;
    case 2: m_screen.clear_line(screen_buffer::clear_type_all);     break;
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_horiz_cursor(const ecma48_code::csi_base& csi)
{
    /* CSI Ps G : Cursor Horizontal Absolute [column] (default = 1) (CHA). */
    int32 column = csi.get_param(0, 1);
    m_screen.set_horiz_cursor(column - 1);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_cursor(const ecma48_code::csi_base& csi)
{
    /* CSI Ps ; Ps H : Cursor Position [row;column] (default = [1,1]) (CUP). */
    int32 row = csi.get_param(0, 1);
    int32 column = csi.get_param(1, 1);
    m_screen.set_cursor(column - 1, row - 1);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::save_cursor()
{
    /* CSI s : Save Current Cursor Position (SCP, SCOSC). */
    m_screen.save_cursor();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::restore_cursor()
{
    /* CSI u : Restore Saved Cursor Position (RCP, SCORC). */
    m_screen.restore_cursor();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::insert_chars(const ecma48_code::csi_base& csi)
{
    /* CSI Ps @  Insert Ps (Blank) Character(s) (default = 1) (ICH). */
    int32 count = csi.get_param(0, 1);
    m_screen.insert_chars(count);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::delete_chars(const ecma48_code::csi_base& csi)
{
    /* CSI Ps P : Delete Ps Character(s) (default = 1) (DCH). */
    int32 count = csi.get_param(0, 1);
    m_screen.delete_chars(count);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_private_mode(const ecma48_code::csi_base& csi)
{
    /* CSI ? Pm h : DEC Private Mode Set (DECSET).
            Ps = 5  -> Reverse Video (DECSCNM).
            Ps = 12 -> Start Blinking Cursor (att610).
            Ps = 25 -> Show Cursor (DECTCEM). */
    for (int32 i = 0; i < csi.param_count; ++i)
    {
        switch (csi.params[i])
        {
        case 12:
            cursor_style(nullptr, 1, -1);
            break;
        case 25:
            cursor_style(nullptr, -1, 1);
            break;
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::reset_private_mode(const ecma48_code::csi_base& csi)
{
    /* CSI ? Pm l : DEC Private Mode Reset (DECRST).
            Ps = 5  -> Normal Video (DECSCNM).
            Ps = 12 -> Stop Blinking Cursor (att610).
            Ps = 25 -> Hide Cursor (DECTCEM). */
    for (int32 i = 0; i < csi.param_count; ++i)
    {
        switch (csi.params[i])
        {
        case 12:
            cursor_style(nullptr, 0, -1);
            break;
        case 25:
            cursor_style(nullptr, -1, 0);
            break;
        }
    }
}

//------------------------------------------------------------------------------
int32 ecma48_terminal_out::build_pending(char c)
{
    if (!m_pending)
    {
        m_ax = 0;
        m_encode_length = 0;
    }

    assert(m_pending < sizeof(m_buffer));
    m_buffer[m_pending++] = c;

    m_ax = (m_ax << 6) | (c & 0x7f);
    if (m_encode_length)
    {
        --m_encode_length;
        return false;
    }

    if ((c & 0xc0) < 0xc0)
        return true;

    if (m_encode_length = !!(c & 0x20))
        m_encode_length += !!(c & 0x10);

    m_ax &= (0x1f >> m_encode_length);
    return (m_pending == sizeof(m_buffer));
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::reset_pending()
{
    m_pending = 0;
}
