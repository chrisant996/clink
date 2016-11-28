// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"

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
    m_set_attr = m_next_attr;
}

//------------------------------------------------------------------------------
attributes printer::get_attributes() const
{
    return m_next_attr;
}
