// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>
#include <terminal/terminal.h>
#include <terminal/terminal_out.h>

//------------------------------------------------------------------------------
class printer;
extern printer* g_printer;

//------------------------------------------------------------------------------
extern "C" int show_cursor(int visible);
extern "C" void use_host_input_mode(void);
extern "C" void use_clink_input_mode(void);

//------------------------------------------------------------------------------
// Scoped configuration of console mode.
//
// Clear 'processed input' flag so key presses such as Ctrl-C and Ctrl-S aren't
// swallowed.  We also want events about window size changes.
class console_config
{
public:
    console_config(HANDLE handle=nullptr);
    ~console_config();

private:
    const HANDLE    m_handle;
    DWORD           m_prev_mode;
};

//------------------------------------------------------------------------------
class printer_context
{
public:
    printer_context(terminal& terminal, printer* printer)
    : m_terminal(terminal)
    , m_rb_printer(g_printer, printer)
    {
        m_terminal.out->open();
        m_terminal.out->begin();
        g_printer = printer;
    }

    ~printer_context()
    {
        m_terminal.out->end();
        m_terminal.out->close();
    }

private:
    const terminal& m_terminal;
    rollback<printer*> m_rb_printer;
};

