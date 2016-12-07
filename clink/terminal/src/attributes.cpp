// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "attributes.h"

static_assert(sizeof(attributes) == sizeof(long long), "sizeof(attr) != 64bits");

//------------------------------------------------------------------------------
enum
{
    default_code = 231, // because xterm256's 231 == old-school colour 15 (white)
};



//------------------------------------------------------------------------------
attributes::attributes()
: m_state(0)
{
}

//------------------------------------------------------------------------------
attributes::attributes(default_e)
: attributes()
{
    reset_fg();
    reset_bg();
    set_bold(false);
    set_underline(false);
}

//------------------------------------------------------------------------------
bool attributes::operator == (const attributes rhs)
{
    int cmp = 1;
    #define CMP_IMPL(x) (m_flags.x & rhs.m_flags.x) ? (m_values.x == rhs.m_values.x) : 1;
    cmp &= CMP_IMPL(fg);
    cmp &= CMP_IMPL(bg);
    cmp &= CMP_IMPL(bold);
    cmp &= CMP_IMPL(underline);
    #undef CMP_IMPL
    return (cmp != 0);
}

//------------------------------------------------------------------------------
attributes attributes::merge(const attributes first, const attributes second)
{
    attributes mask;
    mask.m_flags.all = ~0;
    mask.m_values.fg = second.m_flags.fg ? ~0 : 0;
    mask.m_values.bg = second.m_flags.bg ? ~0 : 0;
    mask.m_values.bold = second.m_flags.bold;
    mask.m_values.underline = second.m_flags.underline;

    attributes out;
    out.m_state = first.m_state & ~mask.m_state;
    out.m_state |= second.m_state & mask.m_state;
    out.m_flags.all |= first.m_flags.all;

    return out;
}

//------------------------------------------------------------------------------
attributes attributes::diff(const attributes from, const attributes to)
{
    flags changed;
    changed.fg = (to.m_values.fg != from.m_values.fg);
    changed.bg = (to.m_values.bg != from.m_values.bg);
    changed.bold = (to.m_values.bold != from.m_values.bold);
    changed.underline = (to.m_values.underline != from.m_values.underline);

    attributes out = to;
    out.m_flags.all &= changed.all;
    return out;
}

//------------------------------------------------------------------------------
void attributes::reset_fg()
{
    m_flags.fg = 1;
    m_values.fg = default_code;
}

//------------------------------------------------------------------------------
void attributes::reset_bg()
{
    m_flags.bg = 1;
    m_values.bg = default_code;
}

//------------------------------------------------------------------------------
void attributes::set_fg(unsigned char value)
{
    if (value == default_code)
        value = 15;

    m_flags.fg = 1;
    m_values.fg = value;
}

//------------------------------------------------------------------------------
void attributes::set_bg(unsigned char value)
{
    if (value == default_code)
        value = 15;

    m_flags.bg = 1;
    m_values.bg = value;
}

//------------------------------------------------------------------------------
void attributes::set_bold(bool state)
{
    m_flags.bold = 1;
    m_values.bold = !!state;
}

//------------------------------------------------------------------------------
void attributes::set_underline(bool state)
{
    m_flags.underline = 1;
    m_values.underline = !!state;
}

//------------------------------------------------------------------------------
attributes::attribute attributes::get_fg() const
{
    return { m_values.fg, m_flags.fg, (m_values.fg == default_code) };
}

//------------------------------------------------------------------------------
attributes::attribute attributes::get_bg() const
{
    return { m_values.bg, m_flags.bg, (m_values.bg == default_code) };
}

//------------------------------------------------------------------------------
attributes::attribute attributes::get_bold() const
{
    return { m_values.bold, m_flags.bold };
}

//------------------------------------------------------------------------------
attributes::attribute attributes::get_underline() const
{
    return { m_values.underline, m_flags.underline };
}
