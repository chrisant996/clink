// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>

//------------------------------------------------------------------------------
class printer;
class terminal_out;
extern printer* g_printer;

//------------------------------------------------------------------------------
extern "C" int32 is_locked_cursor();
extern "C" int32 lock_cursor(int32 lock);
extern "C" int32 show_cursor(int32 visible);
extern "C" int32 cursor_style(HANDLE handle, int32 style, int32 visible);
extern "C" void use_host_input_mode(void);
extern "C" void use_clink_input_mode(void);
extern "C" DWORD select_mouse_input(DWORD mode);
extern "C" void terminal_begin_command();
extern "C" void terminal_end_command();

//------------------------------------------------------------------------------
// Scoped configuration of console mode.
//
// Clear 'processed input' flag so key presses such as Ctrl-C and Ctrl-S aren't
// swallowed.  We also want events about window size changes.
//
// Also initialize ENABLE_MOUSE_INPUT according to setting and terminal state.
class console_config
{
public:
                    console_config(HANDLE handle=nullptr, bool accept_mouse_input=false);
                    ~console_config();
    static void     fix_quick_edit_mode(DWORD& mode);

private:
    static bool     is_mouse_modifier();
    static bool     no_mouse_modifiers();
    const HANDLE    m_handle;
    DWORD           m_prev_mode;
    bool            m_prev_accept_mouse_input;
};

//------------------------------------------------------------------------------
class printer_context
{
public:
    printer_context(terminal_out* terminal, printer* printer);
    printer_context(const printer_context&) = delete;
    ~printer_context();

private:
    terminal_out* const m_terminal;
    rollback<printer*> m_rb_printer;
};

