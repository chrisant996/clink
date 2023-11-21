// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"
#include "terminal_helpers.h"
#include "screen_buffer.h"
#include "cielab.h"

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

extern setting_color g_color_popup;
extern setting_color g_color_popup_desc;

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
static console_theme s_console_theme = console_theme::unknown;
console_theme get_console_theme()
{
    return s_console_theme;
}

//------------------------------------------------------------------------------
static uint8 s_faint_text = 0x80;
#ifdef AUTO_DETECT_CONSOLE_COLOR_THEME
uint8 get_console_faint_text()
{
    return s_faint_text;
}
#endif

//------------------------------------------------------------------------------
static uint8 s_default_attr = 0x07;
uint8 get_console_default_attr()
{
    return s_default_attr;
}

//------------------------------------------------------------------------------
void detect_console_theme()
{
    static HMODULE hmod = GetModuleHandle("kernel32.dll");
    static FARPROC proc = GetProcAddress(hmod, "GetConsoleScreenBufferInfoEx");
    typedef BOOL (WINAPI* GCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    if (!proc)
        return;

    CONSOLE_SCREEN_BUFFER_INFOEX csbix = { sizeof(csbix) };
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GCSBIEx(proc)(h, &csbix))
    {
        s_console_theme = console_theme::unknown;
        s_faint_text = 0x80;
        s_default_attr = 0x07;
        return;
    }

    static const COLORREF c_default_conpty_colors[] =
    {
        RGB(0x0c, 0x0c, 0x0c),
        RGB(0x00, 0x37, 0xda),
        RGB(0x13, 0xa1, 0x0e),
        RGB(0x3a, 0x96, 0xdd),
        RGB(0xc5, 0x0f, 0x1f),
        RGB(0x88, 0x17, 0x98),
        RGB(0xc1, 0x9c, 0x00),
        RGB(0xcc, 0xcc, 0xcc),
        RGB(0x76, 0x76, 0x76),
        RGB(0x3b, 0x78, 0xff),
        RGB(0x16, 0xc6, 0x0c),
        RGB(0x61, 0xd6, 0xd6),
        RGB(0xe7, 0x48, 0x56),
        RGB(0xb4, 0x00, 0x9e),
        RGB(0xf9, 0xf1, 0xa5),
        RGB(0xf2, 0xf2, 0xf2),
    };
    static_assert(sizeof_array(c_default_conpty_colors) == 16, "color table is wrong size");

    s_default_attr = uint8(csbix.wAttributes);

    if (memcmp(csbix.ColorTable, c_default_conpty_colors, sizeof(csbix.ColorTable)) == 0)
    {
        switch (get_current_ansi_handler())
        {
        case ansi_handler::winconsole:
        case ansi_handler::winconsolev2:
            break;
        default:
            s_console_theme = console_theme::default;
            s_faint_text = 0x80;
            return;
        }
    }

    // Luminance range is 0..3000 inclusive.
    #define luminance(cr) \
        (((299 * DWORD(GetRValue(cr))) + \
        (587 * DWORD(GetGValue(cr))) + \
        (114 * DWORD(GetBValue(cr)))) / 85)

    const uint32 lum_bg = luminance(csbix.ColorTable[(csbix.wAttributes & 0xf0) >> 4]);
    if (lum_bg >= luminance(RGB(0x99,0x99,0x99)))
    {
        s_console_theme = console_theme::light;
        s_faint_text = (lum_bg * 255 / 3000) - 0x33;
    }
    else if (lum_bg <= luminance(RGB(0x66,0x66,0x66)))
    {
        s_console_theme = console_theme::dark;
        s_faint_text = (lum_bg * 255 / 3000) + 0x33;
    }
    else
    {
        s_console_theme = console_theme::unknown;
        s_faint_text = 0x80;
    }
}



//------------------------------------------------------------------------------
int32 get_nearest_color(const CONSOLE_SCREEN_BUFFER_INFOEX& csbix, const uint8 (&rgb)[3])
{
    cie::lab target(RGB(rgb[0], rgb[1], rgb[2]));
    double best_deltaE = 0;
    int32 best_idx = -1;

    for (int32 i = sizeof_array(csbix.ColorTable); i--;)
    {
        cie::lab candidate(csbix.ColorTable[i]);
        double deltaE = cie::deltaE_2(target, candidate);
        if (best_idx < 0 || best_deltaE > deltaE)
        {
            best_deltaE = deltaE;
            best_idx = i;
        }
    }

    return best_idx;
}

//------------------------------------------------------------------------------
static constexpr uint8 c_colors[] = { 30, 34, 32, 36, 31, 35, 33, 37, 90, 94, 92, 96, 91, 95, 93, 97 };
const char* get_popup_colors()
{
    static str<32> s_popup;

    str<32> tmp;
    g_color_popup.get(tmp);
    if (!tmp.empty())
    {
        s_popup.format("0;%s", tmp.c_str());
        return s_popup.c_str();
    }

    static HMODULE hmod = GetModuleHandle("kernel32.dll");
    static FARPROC proc = GetProcAddress(hmod, "GetConsoleScreenBufferInfoEx");
    typedef BOOL (WINAPI* GCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    CONSOLE_SCREEN_BUFFER_INFOEX csbiex = { sizeof(csbiex) };
    if (!proc || !GCSBIEx(proc)(GetStdHandle(STD_OUTPUT_HANDLE), &csbiex))
        return "0;30;47";

    WORD attr = csbiex.wPopupAttributes;
    s_popup.format("0;%u;%u", c_colors[attr & 0x0f], c_colors[(attr & 0xf0) >> 4] + 10);
    return s_popup.c_str();
}

//------------------------------------------------------------------------------
const char* get_popup_desc_colors()
{
    static str<32> s_popup_desc;

    str<32> tmp;
    g_color_popup_desc.get(tmp);
    if (!tmp.empty())
    {
        s_popup_desc.format("0;%s", tmp.c_str());
        return s_popup_desc.c_str();
    }

    static HMODULE hmod = GetModuleHandle("kernel32.dll");
    static FARPROC proc = GetProcAddress(hmod, "GetConsoleScreenBufferInfoEx");
    typedef BOOL (WINAPI* GCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    CONSOLE_SCREEN_BUFFER_INFOEX csbiex = { sizeof(csbiex) };
    if (!proc || !GCSBIEx(proc)(GetStdHandle(STD_OUTPUT_HANDLE), &csbiex))
        return "0;90;47";

    int32 dim = 30;
    WORD attr = csbiex.wPopupAttributes;
    if ((attr & 0xf0) == 0x00 || (attr & 0xf0) == 0x10 || (attr & 0xf0) == 0x90)
        dim = 90;
    s_popup_desc.format("0;%u;%u", dim, c_colors[(attr & 0xf0) >> 4] + 10);
    return s_popup_desc.c_str();
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
