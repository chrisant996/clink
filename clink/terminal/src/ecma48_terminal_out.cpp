// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ecma48_terminal_out.h"

#include <core/array.h>
#include <core/base.h>

//------------------------------------------------------------------------------
ecma48_terminal_out::ecma48_terminal_out(terminal_out& inner)
: m_inner(inner)
{
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::begin()
{
    m_inner.begin();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::end()
{
    m_inner.end();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::flush()
{
    m_inner.flush();
}

//------------------------------------------------------------------------------
int ecma48_terminal_out::get_columns() const
{
    return m_inner.get_columns();
}

//------------------------------------------------------------------------------
int ecma48_terminal_out::get_rows() const
{
    return m_inner.get_rows();
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_attributes(const attributes attr)
{
    m_inner.set_attributes(attr);
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::write_c1(const ecma48_code& code)
{
    if (code.get_code() != ecma48_code::c1_csi)
        return;

    ecma48_code::csi<32> csi;
    code.decode_csi(csi);

    const array<int> params_array(csi.params, csi.param_count);

    switch (csi.final)
    {
    case 'm':
        write_sgr(params_array);
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
            m_inner.write(code->get_pointer(), code->get_length());
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
void ecma48_terminal_out::write_sgr(const array<int>& params)
{
    // Empty parameters to 'CSI SGR' implies 0 (reset).
    if (params.empty())
        return set_attributes(attributes::defaults);

    // Process each code that is supported.
    attributes attr;
    for (unsigned int param : params)
    {
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

    set_attributes(attr);
}
