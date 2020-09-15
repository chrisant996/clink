// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ecma48_terminal_out.h"
#include "ecma48_iter.h"
#include "screen_buffer.h"

//------------------------------------------------------------------------------
ecma48_terminal_out::ecma48_terminal_out(screen_buffer& screen)
: m_screen(screen)
{
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::begin()
{
    m_screen.begin();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::end()
{
    m_screen.end();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::flush()
{
    m_screen.flush();
}

//------------------------------------------------------------------------------
int ecma48_terminal_out::get_columns() const
{
    return m_screen.get_columns();
}

//------------------------------------------------------------------------------
int ecma48_terminal_out::get_rows() const
{
    return m_screen.get_rows();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write_c1(const ecma48_code& code)
{
    if (code.get_code() != ecma48_code::c1_csi)
        return;

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
        case 'G': m_screen.move_cursor(-m_screen.get_columns(), 0); break;
        case 'H': set_cursor(csi);          break;
        case 'J': erase_in_display(csi);    break;
        case 'K': erase_in_line(csi);       break;
        case 'P': delete_chars(csi);        break;
        case 'm': set_attributes(csi);      break;

        case 'A': m_screen.move_cursor(0, -csi.get_param(0, 1)); break;
        case 'B': m_screen.move_cursor(0,  csi.get_param(0, 1)); break;
        case 'C': m_screen.move_cursor( csi.get_param(0, 1), 0); break;
        case 'D': m_screen.move_cursor(-csi.get_param(0, 1), 0); break;
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write_c0(int c0)
{
    switch (c0)
    {
    case ecma48_code::c0_bel:
        // TODO: flash cursor.
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
void ecma48_terminal_out::write(const char* chars, int length)
{
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
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_attributes(const ecma48_code::csi_base& csi)
{
    // Empty parameters to 'CSI SGR' implies 0 (reset).
    if (csi.param_count == 0)
        return m_screen.set_attributes(attributes::defaults);

    // Process each code that is supported.
    attributes attr;
    for (int i = 0; i < csi.param_count; ++i)
    {
        unsigned int param = csi.params[i];

        // Resets
        if (param == 0)  { attr = attributes::defaults; continue; }
        if (param == 49) { attr.reset_bg();             continue; }
        if (param == 39) { attr.reset_fg();             continue; }

        // Bold/Underline
        if ((param == 1) | (param == 2) | (param == 22))
        {
            attr.set_bold(param == 1);
            continue;
        }

        if ((param == 4) | (param == 24))
        {
            attr.set_underline(param == 4);
            continue;
        }

        // Fore/background colours.
        if ((param - 30 < 8) | (param - 90 < 8))
        {
            param += (param >= 90) ? 14 : 2;
            attr.set_fg(param & 0x0f);
            continue;
        }

        if ((param - 40 < 8) | (param - 100 < 8))
        {
            param += (param >= 100) ? 4 : 8;
            attr.set_bg(param & 0x0f);
            continue;
        }

        // TODO: Rgb/xterm256 support for terminals that support it.
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
void ecma48_terminal_out::set_cursor(const ecma48_code::csi_base& csi)
{
    /* CSI Ps ; Ps H : Cursor Position [row;column] (default = [1,1]) (CUP). */
    int row = csi.get_param(0, 1);
    int column = csi.get_param(1, 1);
    m_screen.set_cursor(column - 1, row - 1);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::insert_chars(const ecma48_code::csi_base& csi)
{
    /* CSI Ps @  Insert Ps (Blank) Character(s) (default = 1) (ICH). */
    int count = csi.get_param(0, 1);
    m_screen.insert_chars(count);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::delete_chars(const ecma48_code::csi_base& csi)
{
    /* CSI Ps P : Delete Ps Character(s) (default = 1) (DCH). */
    int count = csi.get_param(0, 1);
    m_screen.delete_chars(count);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_private_mode(const ecma48_code::csi_base& csi)
{
    /* CSI ? Pm h : DEC Private Mode Set (DECSET).
            Ps = 5  -> Reverse Video (DECSCNM).
            Ps = 12 -> Start Blinking Cursor (att610).
            Ps = 25 -> Show Cursor (DECTCEM). */
    for (int i = 0; i < csi.param_count; ++i)
    {
        switch (csi.params[i])
        {
        case 12: /* TODO */ break;
        case 25: /* TODO */ break;
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
    for (int i = 0; i < csi.param_count; ++i)
    {
        switch (csi.params[i])
        {
        case 12: /* TODO */ break;
        case 25: /* TODO */ break;
        }
    }
}
