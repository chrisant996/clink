// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"
#include "terminal_helpers.h"
#include "screen_buffer.h"

#include <core/settings.h>
#include <core/os.h>
#include <core/log.h>

#include <assert.h>

//------------------------------------------------------------------------------
extern bool g_enhanced_cursor;
printer* g_printer = nullptr;
bool g_accept_mouse_input = false;

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

static setting_enum g_mouse_input(
    "terminal.mouse_input",
    "Clink mouse input",
    "Clink can optionally respond to mouse input, instead of letting the terminal\n"
    "respond to mouse input (e.g. to select text on the screen).  When mouse input\n"
    "is enabled in Clink, clicking in the input line sets the cursor position, and\n"
    "clicking in popup lists selects an item, etc.\n"
    "\n"
    "'off' lets the terminal host handle mouse input.\n"
    "'on' lets Clink handle mouse input.\n"
    "'auto' lets Clink handle mouse input in ConEmu and in the default Conhost\n"
    "terminal when Quick Edit mode is unchecked in the console Properties dialog.\n"
    "\n"
    "NOTES:\n"
    "- ConEmu does not let Clink respond to the mouse wheel.\n"
    "- Windows Terminal does not let Clink scroll the terminal, but you can scroll\n"
    "  by holding Shift or Alt while using the mouse wheel.\n"
    "- Holding Shift, Ctrl, or Alt while clicking allows the normal terminal mouse\n"
    "  input to still work (for example, to select text on the screen).",
    "off,on,auto",
    2);

static setting_str g_mouse_modifier(
    "terminal.mouse_modifier",
    "Modifier keys for mouse input",
    "This selects which modifier keys (Alt, Ctrl, Shift) must be held in order\n"
    "for Clink to respond to mouse input when mouse input is enabled by the\n"
    "'terminal.mouse_input' setting.\n"
    "\n"
    "This is a text string that can list one or more modifier keys:  'alt', 'ctrl',\n"
    "and 'shift'.  For example, setting it to \"alt shift\" causes Clink to only\n"
    "respond to mouse input when both Alt and Shift are held (and not Ctrl).\n"
    "If the %CLINK_MOUSE_MODIFIER% environment variable is set then its value\n"
    "supersedes this setting.\n"
    "\n"
    "Note that in the default Conhost terminal when Quick Edit mode is turned off\n"
    "then Clink will also respond to mouse input when no modifier keys are held.",
    "");

//------------------------------------------------------------------------------
static bool s_locked_cursor_visibility = false;
extern "C" int32 is_locked_cursor()
{
    return s_locked_cursor_visibility;
}

//------------------------------------------------------------------------------
extern "C" int32 lock_cursor(int32 lock)
{
    assert(!lock || !s_locked_cursor_visibility);
    bool was_locked = s_locked_cursor_visibility;
    s_locked_cursor_visibility = !!lock;
    return was_locked;
}

//------------------------------------------------------------------------------
extern "C" int32 cursor_style(HANDLE handle, int32 style, int32 visible)
{
    if (!handle)
        handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(handle, &ci);
    int32 was_visible = !!ci.bVisible;

    // Assume first encounter of cursor size is the default size.  This only
    // works for Use Legacy Style; the newer cursor shapes all report 25.
    static int32 g_default_cursor_size = -1;
    static int32 g_alternate_cursor_size = 100;
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
static void save_host_input_mode(DWORD mode)
{
    s_host_input_mode = mode;
}

//------------------------------------------------------------------------------
extern "C" DWORD cleanup_console_input_mode(DWORD mode)
{
#ifdef DEBUG
    if (mode & ENABLE_PROCESSED_INPUT)
        LOG("CONSOLE MODE: console input mode has ENABLE_PROCESSED_INPUT set (0x%x, bit 0x%x)", mode, ENABLE_PROCESSED_INPUT);
    if (mode & ENABLE_VIRTUAL_TERMINAL_INPUT)
        LOG("CONSOLE MODE: console input mode has ENABLE_VIRTUAL_TERMINAL_INPUT set (0x%x, bit 0x%x)", mode, ENABLE_VIRTUAL_TERMINAL_INPUT);
#endif

    // Clear modes that are incompatible with Clink:

    // ENABLE_PROCESSED_INPUT can happen when Lua code uses io.popen():lines()
    // and returns without finishing reading the output, or uses os.execute() in
    // a coroutine.
    mode &= ~ENABLE_PROCESSED_INPUT;

    // ENABLE_VIRTUAL_TERMINAL_INPUT can happen when console programs change
    // the input mode and either doesn't clean it up before exiting or continues
    // running in the background after returning control to its parent process.

    mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;

    return mode;
}

//------------------------------------------------------------------------------
extern "C" void use_host_input_mode(void)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE)
    {
        DWORD mode;
        if (GetConsoleMode(h, &mode))
            s_clink_input_mode = cleanup_console_input_mode(mode);

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
static bool s_mouse_alt = false;
static bool s_mouse_ctrl = false;
static bool s_mouse_shift = false;
static bool s_quick_edit = false;

//------------------------------------------------------------------------------
DWORD select_mouse_input(DWORD mode)
{
    if (!g_accept_mouse_input)
        return mode & ~ENABLE_MOUSE_INPUT;

    switch (g_mouse_input.get())
    {
    default:
    case 0:
        // Off.
        break;
    case 1:
        // On.
        switch (get_native_ansi_handler())
        {
        case ansi_handler::conemu:
            mode |= ENABLE_MOUSE_INPUT;
            break;
        default:
            mode &= ~ENABLE_QUICK_EDIT_MODE;
            mode |= ENABLE_MOUSE_INPUT;
            break;
        }
        break;
    case 2:
        // Auto.
        switch (get_native_ansi_handler())
        {
        case ansi_handler::conemu:
            mode |= ENABLE_MOUSE_INPUT;
            break;
        default:
            if (s_mouse_alt || s_mouse_ctrl || s_mouse_shift)
                mode |= ENABLE_MOUSE_INPUT;
            else if (get_native_ansi_handler() == ansi_handler::winterminal ||
                     get_native_ansi_handler() == ansi_handler::wezterm)
                mode &= ~ENABLE_MOUSE_INPUT;
            else if (!(mode & ENABLE_QUICK_EDIT_MODE))
                mode |= ENABLE_MOUSE_INPUT;
            break;
        }
        break;
    }

    return mode;
}

//------------------------------------------------------------------------------
static bool strstri(const char* needle, const char* haystack)
{
    const size_t len = strlen(needle);
    while (*haystack)
    {
        if (_strnicmp(needle, haystack, len) == 0)
            return true;
        haystack++;
    }
    return false;
}

//------------------------------------------------------------------------------
console_config::console_config(HANDLE handle, bool accept_mouse_input)
    : m_handle(handle ? handle : GetStdHandle(STD_INPUT_HANDLE))
{
    GetConsoleMode(m_handle, &m_prev_mode);
    save_host_input_mode(m_prev_mode);

    m_prev_accept_mouse_input = g_accept_mouse_input;
    g_accept_mouse_input = accept_mouse_input;

    s_quick_edit = !!(m_prev_mode & ENABLE_QUICK_EDIT_MODE);

    str<16> tmp;
    if (!os::get_env("clink_mouse_modifier", tmp) || tmp.empty())
        g_mouse_modifier.get(tmp);
    s_mouse_alt = !!strstri("alt", tmp.c_str());
    s_mouse_ctrl = !!strstri("ctrl", tmp.c_str());
    s_mouse_shift = !!strstri("shift", tmp.c_str());

    // NOTE:  Windows Terminal doesn't reliably respond to changes of the
    // ENABLE_MOUSE_INPUT flag when ENABLE_AUTO_POSITION is missing.
    DWORD mode = m_prev_mode;
    mode &= ~(ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_MOUSE_INPUT);
    mode |= ENABLE_WINDOW_INPUT;
    mode = select_mouse_input(mode);
    mode = cleanup_console_input_mode(mode);
    SetConsoleMode(m_handle, mode);
}

//------------------------------------------------------------------------------
console_config::~console_config()
{
    SetConsoleMode(m_handle, m_prev_mode);
    g_accept_mouse_input = m_prev_accept_mouse_input;
}

//------------------------------------------------------------------------------
void console_config::fix_quick_edit_mode(DWORD& mode)
{
    if (!g_accept_mouse_input)
        return;

    switch (get_native_ansi_handler())
    {
    case ansi_handler::conemu:
        break;

    case ansi_handler::winterminal:
    case ansi_handler::wezterm:
        if ((s_mouse_alt || s_mouse_ctrl || s_mouse_shift) && is_mouse_modifier())
        {
            mode &= ~ENABLE_QUICK_EDIT_MODE;
        }
        else
        {
            mode |= ENABLE_QUICK_EDIT_MODE;
        }
        break;

    default:
        if (s_mouse_alt || s_mouse_ctrl || s_mouse_shift)
        {
            if (is_mouse_modifier() || (!s_quick_edit && no_mouse_modifiers()))
                mode &= ~ENABLE_QUICK_EDIT_MODE;
            else
                mode |= ENABLE_QUICK_EDIT_MODE;
        }
        else
        {
            if (is_mouse_modifier())
                mode &= ~ENABLE_QUICK_EDIT_MODE;
            else
                mode |= ENABLE_QUICK_EDIT_MODE;
        }
        break;
    }
}

//------------------------------------------------------------------------------
bool console_config::is_mouse_modifier()
{
    return ((GetKeyState(VK_SHIFT) < 0) == s_mouse_shift &&
            (GetKeyState(VK_CONTROL) < 0) == s_mouse_ctrl &&
            (GetKeyState(VK_MENU) < 0) == s_mouse_alt);
}

//------------------------------------------------------------------------------
bool console_config::no_mouse_modifiers()
{
    return (!(GetKeyState(VK_SHIFT) < 0) &&
            !(GetKeyState(VK_CONTROL) < 0) &&
            !(GetKeyState(VK_MENU) < 0));
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
