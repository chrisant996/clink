// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal_helpers.h"

#include <core/settings.h>
#include <terminal/printer.h>
#include <terminal/terminal_out.h>

#include <assert.h>



//------------------------------------------------------------------------------
printer* g_printer = nullptr;



//------------------------------------------------------------------------------
extern setting_bool g_adjust_cursor_style;
static bool s_locked_cursor_visibility = false;

//------------------------------------------------------------------------------
extern "C" int is_locked_cursor()
{
    return s_locked_cursor_visibility;
}

//------------------------------------------------------------------------------
extern "C" int lock_cursor(int lock)
{
    assert(!lock || !s_locked_cursor_visibility);
    bool was_locked = s_locked_cursor_visibility;
    s_locked_cursor_visibility = !!lock;
    return was_locked;
}

//------------------------------------------------------------------------------
extern "C" int show_cursor(int visible)
{
    int was_visible = 0;

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    was_visible = (GetConsoleCursorInfo(handle, &info) && info.bVisible);

    if (!g_adjust_cursor_style.get())
        return was_visible;

    if (!was_visible != !visible && !s_locked_cursor_visibility)
    {
        info.bVisible = !!visible;
        SetConsoleCursorInfo(handle, &info);
    }

    return was_visible;
}



//------------------------------------------------------------------------------
static DWORD s_host_input_mode = -1;
static DWORD s_clink_input_mode = -1;

//------------------------------------------------------------------------------
void save_host_input_mode(DWORD mode)
{
    s_host_input_mode = mode;
}

//------------------------------------------------------------------------------
extern "C" void use_host_input_mode(void)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE)
    {
        DWORD mode;
        if (GetConsoleMode(h, &mode))
            s_clink_input_mode = mode;

        if (s_host_input_mode != -1)
            SetConsoleMode(h, s_host_input_mode);
    }
}

//------------------------------------------------------------------------------
extern "C" void use_clink_input_mode(void)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE)
    {
        DWORD mode;
        if (s_host_input_mode == -1 && GetConsoleMode(h, &mode))
            s_host_input_mode = mode;

        if (s_clink_input_mode != -1)
            SetConsoleMode(h, s_clink_input_mode);
    }
}



//------------------------------------------------------------------------------
console_config::console_config(HANDLE handle)
    : m_handle(handle ? handle : GetStdHandle(STD_INPUT_HANDLE))
{
    extern void save_host_input_mode(DWORD);
    GetConsoleMode(m_handle, &m_prev_mode);
    save_host_input_mode(m_prev_mode);
    SetConsoleMode(m_handle, ENABLE_WINDOW_INPUT);
}

console_config::~console_config()
{
    SetConsoleMode(m_handle, m_prev_mode);
}



//------------------------------------------------------------------------------
printer_context::printer_context(terminal_out* terminal, printer* printer)
: m_terminal(terminal)
, m_rb_printer(g_printer)
{
    m_terminal->open();
    m_terminal->begin();

    assert(!g_printer);
    g_printer = printer;
}

//------------------------------------------------------------------------------
printer_context::~printer_context()
{
    m_terminal->end();
    m_terminal->close();
}
