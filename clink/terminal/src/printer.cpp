// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"

#include <core/str.h>

//------------------------------------------------------------------------------
printer::printer(terminal_out& terminal)
: m_terminal(terminal)
, m_set_attr(attributes::defaults)
, m_next_attr(attributes::defaults)
{
}

//------------------------------------------------------------------------------
void printer::print(const char* data, int bytes)
{
    if (bytes <= 0)
        return;

    if (m_next_attr != m_set_attr)
        flush_attributes();

    m_terminal.write(data, bytes);
}

//------------------------------------------------------------------------------
void printer::print(const attributes attr, const char* data, int bytes)
{
    attributes prev_attr = set_attributes(attr);
    print(data, bytes);
    set_attributes(prev_attr);
}

//------------------------------------------------------------------------------
unsigned int printer::get_columns() const
{
    return m_terminal.get_columns();
}

//------------------------------------------------------------------------------
unsigned int printer::get_rows() const
{
    return m_terminal.get_rows();
}

//------------------------------------------------------------------------------
attributes printer::set_attributes(const attributes attr)
{
    attributes prev_attr = m_next_attr;
    m_next_attr = attributes::merge(m_next_attr, attr);
    return prev_attr;
}

//------------------------------------------------------------------------------
void printer::flush_attributes()
{
    attributes diff = attributes::diff(m_set_attr, m_next_attr);

    str<64, false> params;
    auto add_param = [&] (const char* x) {
        if (!params.empty())
            params << ";";
        params << x;
    };

    auto fg = diff.get_fg();
    auto bg = diff.get_bg();
    if (fg.is_default & bg.is_default)
    {
        add_param("0");
    }
    else
    {
        if (fg)
        {
            if (!fg.is_default)
            {
                char x[] = "30";
                x[0] += (fg.value > 7) ? 6 : 0;
                x[1] += fg.value & 0x07;
                add_param(x);
            }
            else
                add_param("39");
        }

        if (bg)
        {
            if (!bg.is_default)
            {
                char x[] = "100";
                x[1] += (bg.value > 7) ? 0 : 4;
                x[2] += bg.value & 0x07;
                add_param((bg.value > 7) ? x : x + 1);
            }
            else
                add_param("49");
        }
    }

    if (auto bold = diff.get_bold())
        add_param(bold.value ? "1" : "22");

    if (auto underline = diff.get_underline())
        add_param(underline.value ? "4" : "24");

    if (!params.empty())
    {
        m_terminal.write("\x1b[");
        m_terminal.write(params.c_str(), params.length());
        m_terminal.write("m");
    }

    m_set_attr = m_next_attr;
}

//------------------------------------------------------------------------------
attributes printer::get_attributes() const
{
    return m_next_attr;
}
