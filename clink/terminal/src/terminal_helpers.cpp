// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"
#include "terminal_helpers.h"

#include <core/settings.h>

#include <assert.h>

//------------------------------------------------------------------------------
extern bool g_enhanced_cursor;
printer* g_printer = nullptr;

//------------------------------------------------------------------------------
setting_bool g_adjust_cursor_style(
    "terminal.adjust_cursor_style",
    "Adjusts the cursor visibility and shape",
    "Normally Clink adjusts the cursor visibility and shape, but that will override\n"
    "the Cursor Shape settings for the default Windows console.  Disabling this\n"
    "lets the Cursor Shape settings work, but then Clink can't show Insert Mode via\n"
    "the cursor shape, the 'visible bell' setting doesn't work, Clink can't support\n"
    "the ANSI escape codes for cursor shape, and the cursor may flicker or flash\n"
    "strangely while typing.",
    true);

//------------------------------------------------------------------------------
static bool s_locked_cursor_visibility = false;
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
extern "C" int cursor_style(HANDLE handle, int style, int visible)
{
    if (!handle)
        handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(handle, &ci);
    int was_visible = !!ci.bVisible;

    // Assume first encounter of cursor size is the default size.  This only
    // works for Use Legacy Style; the newer cursor shapes all report 25.
    static int g_default_cursor_size = -1;
    static int g_alternate_cursor_size = 100;
    if (g_default_cursor_size < 0)
    {
        g_default_cursor_size = ci.dwSize;
        if (g_default_cursor_size >= 75)
            g_alternate_cursor_size = 50;
    }

    if (is_locked_cursor() || !g_adjust_cursor_style.get())
        return was_visible;
    if (style < 0 && visible < 0)
        return was_visible;

    if (style < 0)
        style = g_enhanced_cursor;
    else
        g_enhanced_cursor = !!style;

    ci.dwSize = style ? g_alternate_cursor_size : g_default_cursor_size;

    if (visible >= 0)
        ci.bVisible = !!visible;

    SetConsoleCursorInfo(handle, &ci);

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

    DWORD mode = m_prev_mode;
    mode &= ~(ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_MOUSE_INPUT);
    mode |= ENABLE_WINDOW_INPUT;
    SetConsoleMode(m_handle, mode);
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
