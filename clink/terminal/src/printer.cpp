// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"

//------------------------------------------------------------------------------
printer::printer(terminal_out& terminal)
: m_terminal(terminal)
{
}

//------------------------------------------------------------------------------
unsigned int printer::print(const char* data, int bytes)
{
    m_terminal.write(data, bytes);
    return 0;
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
