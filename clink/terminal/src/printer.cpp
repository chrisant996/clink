// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"

#include <core/str.h>

//------------------------------------------------------------------------------
static bool s_is_scrolled = false;
void set_scrolled_screen_buffer()
{
    s_is_scrolled = true;
}



//------------------------------------------------------------------------------
printer::printer(terminal_out& terminal)
: m_terminal(terminal)
, m_nodiff(false)
{
    reset();
}

//------------------------------------------------------------------------------
void printer::reset()
{
    m_set_attr = attributes::defaults;
    m_next_attr = attributes::defaults;
    m_nodiff = false;
}

//------------------------------------------------------------------------------
void printer::print(const char* data, int32 bytes)
{
    if (bytes <= 0)
        return;

    // HACK: Work around a problem where WriteConsoleW(" ") after using
    // ScrollConsoleRelative() to scroll the cursor line past the bottom of the
    // screen window clears screen attributes from the prompt (but not if
    // scrolled the other direction, and not if the scrollbar was used to scroll
    // the screen buffer!?).
    // NOTE: Must happen before flush_attributes(), otherwise it will try to
    // write VT escape codes while the console is scrolled, and the color codes
    // get ignored.
    if (s_is_scrolled)
    {
        m_terminal.flush();
        s_is_scrolled = false;
    }

    if (m_next_attr != m_set_attr)
        flush_attributes();

    m_terminal.write(data, bytes);
}

//------------------------------------------------------------------------------
void printer::print(const attributes attr, const char* data, int32 bytes)
{
    attributes prev_attr = set_attributes(attr);
    print(data, bytes);
    set_attributes(prev_attr);
    flush_attributes();
}

//------------------------------------------------------------------------------
void printer::print(const char* attr, const char* data, int32 bytes)
{
    str<> tmp;
    tmp.format("\x1b[%sm", attr);

    attributes prev_attr = m_next_attr;
    print(tmp.c_str(), tmp.length());
    print(data, bytes);
    set_attributes(prev_attr);
    flush_attributes();
    m_nodiff = true;
}

//------------------------------------------------------------------------------
uint32 printer::get_columns() const
{
    return m_terminal.get_columns();
}

//------------------------------------------------------------------------------
uint32 printer::get_rows() const
{
    return m_terminal.get_rows();
}

//------------------------------------------------------------------------------
bool printer::get_line_text(int32 line, str_base& out) const
{
    return m_terminal.get_line_text(line, out);
}

//------------------------------------------------------------------------------
int32 printer::is_line_default_color(int32 line) const
{
    return m_terminal.is_line_default_color(line);
}

//------------------------------------------------------------------------------
int32 printer::line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask) const
{
    return m_terminal.line_has_color(line, attrs, num_attrs, mask);
}

//------------------------------------------------------------------------------
int32 printer::find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs, int32 num_attrs, BYTE mask) const
{
    return m_terminal.find_line(starting_line, distance, text, mode, attrs, num_attrs, mask);
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
    attributes diff = m_nodiff ? m_next_attr : attributes::diff(m_set_attr, m_next_attr);

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
                x[0] += (fg->value > 7) ? 6 : 0;
                x[1] += fg->value & 0x07;
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
                x[1] += (bg->value > 7) ? 0 : 4;
                x[2] += bg->value & 0x07;
                add_param((bg->value > 7) ? x : x + 1);
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

//------------------------------------------------------------------------------
void printer::insert(int32 count)
{
}

//------------------------------------------------------------------------------
void printer::move_cursor(int32 dc, int32 dr)
{
}

//------------------------------------------------------------------------------
void printer::set_cursor(cursor_state state)
{
}

//------------------------------------------------------------------------------
printer::cursor_state printer::get_cursor() const
{
    return 0;
}
