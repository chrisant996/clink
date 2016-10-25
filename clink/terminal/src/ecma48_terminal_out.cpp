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

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(handle, &csbi);
    m_default_attr = csbi.wAttributes & 0xff;
    m_attr = m_default_attr;
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::end()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(handle, m_default_attr);
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
void ecma48_terminal_out::write_c1(const ecma48_code& code)
{
    if (code.get_code() != ecma48_code::c1_csi)
        return;

    int final, params[32], param_count;
    param_count = code.decode_csi(final, params, sizeof_array(params));
    const array<int> params_array(params, param_count);

    switch (final)
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
    static const unsigned char sgr_to_attr[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

    // Process each code that is supported.
    unsigned char attr = get_attr();
    for (unsigned int param : params)
    {
        if (param == 0) // reset
        {
            attr = get_default_attr();
        }
        else if (param == 1) // fg intensity (bright)
        {
            attr |= 0x08;
        }
        else if (param == 2 || param == 22) // fg intensity (normal)
        {
            attr &= ~0x08;
        }
        else if (param == 4) // bg intensity (bright)
        {
            attr |= 0x80;
        }
        else if (param == 24) // bg intensity (normal)
        {
            attr &= ~0x80;
        }
        else if (param - 30 < 8) // fg colour
        {
            attr = (attr & 0xf8) | sgr_to_attr[(param - 30) & 7];
        }
        else if (param - 90 < 8) // fg colour
        {
            attr |= 0x08;
            attr = (attr & 0xf8) | sgr_to_attr[(param - 90) & 7];
        }
        else if (param == 39) // default fg colour
        {
            attr = (attr & 0xf8) | (get_default_attr() & 0x07);
        }
        else if (param - 40 < 8) // bg colour
        {
            attr = (attr & 0x8f) | (sgr_to_attr[(param - 40) & 7] << 4);
        }
        else if (param - 100 < 8) // bg colour
        {
            attr |= 0x80;
            attr = (attr & 0x8f) | (sgr_to_attr[(param - 100) & 7] << 4);
        }
        else if (param == 49) // default bg colour
        {
            attr = (attr & 0x8f) | (get_default_attr() & 0x70);
        }
        else if (param == 38 || param == 48) // extended colour (skipped)
            continue;
    }

    set_attr(attr);
}

//------------------------------------------------------------------------------
unsigned char ecma48_terminal_out::get_default_attr() const
{
    return m_default_attr;
}

//------------------------------------------------------------------------------
unsigned char ecma48_terminal_out::get_attr() const
{
    return m_attr;
}

//------------------------------------------------------------------------------
void ecma48_terminal_out::set_attr(unsigned char attr)
{
    m_attr = attr;

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(handle, attr);
}
