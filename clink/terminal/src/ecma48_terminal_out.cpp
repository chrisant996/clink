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

    switch (csi.final)
    {
    case 'm':
        set_attributes(csi);
        break;
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write_c0(int c0)
{
    switch (c0)
    {
    case ecma48_code::c0_bel:
        // TODO
        break;

    case ecma48_code::c0_ht:
    case ecma48_code::c0_bs:
    case ecma48_code::c0_lf:
    case ecma48_code::c0_cr:
        {
            char c = char(c0);
            m_inner.write(&c, 1);
            break;
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write(const char* chars, int length)
{
    ecma48_iter iter(chars, m_state, length);
    while (const ecma48_code* code = iter.next())
    {
        switch (code->get_type())
        {
        case ecma48_code::type_chars:
            m_screen.write(code->get_pointer(), code->get_length());
            break;

        case ecma48_code::type_c0:
            write_c0(code->get_code());
            break;

        case ecma48_code::type_c1:
            write_c1(*code);
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
