// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "printer.h"
#include "terminal_out.h"
#include "terminal_helpers.h"
#include "screen_buffer.h"
#include "ecma48_iter.h"
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

static setting_color g_color_popup(
    "color.popup",
    "Color for popup lists and messages",
    "Used when Clink shows a text mode popup list or message, for example when\n"
    "using the win-history-list command bound by default to F7.  If not set, the\n"
    "console's popup colors are used.",
    "");

static setting_color g_color_popup_desc(
    "color.popup_desc",
    "Color for popup description column(s)",
    "Used when Clink shows multiple columns of text in a text mode popup list.\n"
    "If not set, a color is chosen to complement the console's popup colors.",
    "");

static setting_color g_color_popup_border(
    "color.popup_border",
    "Color for popup border",
    "If not set, the color from color.popup is used.",
    "");

static setting_color g_color_popup_header(
    "color.popup_header",
    "Color for popup title text",
    "If not set, the color from color.popup_border is used.",
    "");

static setting_color g_color_popup_footer(
    "color.popup_footer",
    "Color for popup footer message",
    "If not set, the color from color.popup_border is used.",
    "");

static setting_color g_color_popup_select(
    "color.popup_select",
    "Color for selected item in popup list",
    "If not set, a color is chosen by swapping the foreground and background\n"
    "colors from color.popup.",
    "");

static setting_color g_color_popup_selectdesc(
    "color.popup_selectdesc",
    "Color for selected item in popup list",
    "If not set, the color from color.popup_select is used.",
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
        handle = get_std_handle(STD_OUTPUT_HANDLE);

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
    HANDLE h = get_std_handle(STD_OUTPUT_HANDLE);
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
            s_console_theme = console_theme::system;
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

    // FUTURE: consider using Oklab instead?
    // https://bottosson.github.io/posts/oklab/
    // https://github.com/chrisant996/dirx/blob/90d9f7422fee098776375360fb1a40b4d3da52e9/colors.cpp#L1780-L1847
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
    static str<48> s_popup;

    str<48> tmp;
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
    if (!proc || !GCSBIEx(proc)(get_std_handle(STD_OUTPUT_HANDLE), &csbiex))
        return "0;30;47";

    WORD attr = csbiex.wPopupAttributes;
    s_popup.format("0;%u;%u", c_colors[attr & 0x0f], c_colors[(attr & 0xf0) >> 4] + 10);
    return s_popup.c_str();
}

//------------------------------------------------------------------------------
const char* get_popup_desc_colors()
{
    static str<48> s_popup_desc;

    str<48> tmp;
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
    if (!proc || !GCSBIEx(proc)(get_std_handle(STD_OUTPUT_HANDLE), &csbiex))
        return "0;90;47";

    int32 dim = 30;
    WORD attr = csbiex.wPopupAttributes;
    if ((attr & 0xf0) == 0x00 || (attr & 0xf0) == 0x10 || (attr & 0xf0) == 0x90)
        dim = 90;
    s_popup_desc.format("0;%u;%u", dim, c_colors[(attr & 0xf0) >> 4] + 10);
    return s_popup_desc.c_str();
}

//------------------------------------------------------------------------------
const char* get_popup_border_colors(const char* preferred)
{
    static str<48> s_popup_border;

    if (preferred && *preferred)
    {
        s_popup_border = preferred;
    }
    else
    {
        str<48> tmp;
        g_color_popup_border.get(tmp);
        if (!tmp.empty())
            s_popup_border.format("0;%s", tmp.c_str());
        else
            s_popup_border = get_popup_colors();
    }
    return s_popup_border.c_str();
}

//------------------------------------------------------------------------------
const char* get_popup_header_colors(const char* preferred)
{
    static str<48> s_popup_header;

    if (preferred && *preferred)
    {
        s_popup_header = preferred;
    }
    else
    {
        str<48> tmp;
        g_color_popup_header.get(tmp);
        if (!tmp.empty())
            s_popup_header.format("0;%s", tmp.c_str());
        else
            s_popup_header = get_popup_border_colors();
    }
    return s_popup_header.c_str();
}

//------------------------------------------------------------------------------
const char* get_popup_footer_colors(const char* preferred)
{
    static str<48> s_popup_footer;

    if (preferred && *preferred)
    {
        s_popup_footer = preferred;
    }
    else
    {
        str<48> tmp;
        g_color_popup_footer.get(tmp);
        if (!tmp.empty())
            s_popup_footer.format("0;%s", tmp.c_str());
        else
            s_popup_footer = get_popup_border_colors();
    }
    return s_popup_footer.c_str();
}

//------------------------------------------------------------------------------
const char* get_popup_select_colors(const char* preferred)
{
    static str<48> s_popup_select;

    if (preferred && *preferred)
    {
        s_popup_select.format("0;%s;7", preferred);
    }
    else
    {
        str<48> tmp;
        g_color_popup_select.get(tmp);
        if (!tmp.empty())
            s_popup_select.format("0;%s", tmp.c_str());
        else
            s_popup_select.format("0;%s;7", get_popup_colors());
    }
    return s_popup_select.c_str();
}

//------------------------------------------------------------------------------
const char* get_popup_selectdesc_colors(const char* preferred)
{
    static str<48> s_popup_selectdesc;

    if (preferred && *preferred)
    {
        s_popup_selectdesc = preferred;
    }
    else
    {
        str<48> tmp;
        g_color_popup_selectdesc.get(tmp);
        if (!tmp.empty())
            s_popup_selectdesc.format("0;%s", tmp.c_str());
        else
            s_popup_selectdesc = get_popup_select_colors();
    }
    return s_popup_selectdesc.c_str();
}



//------------------------------------------------------------------------------
static HANDLE s_override_hstdin = 0;
static HANDLE s_override_hstdout = 0;

//------------------------------------------------------------------------------
void override_stdio_handles(HANDLE hin, HANDLE hout)
{
    s_override_hstdin = hin;
    s_override_hstdout = hout;
}

//------------------------------------------------------------------------------
extern "C" HANDLE get_std_handle(DWORD n)
{
    switch (n)
    {
    case STD_INPUT_HANDLE:
        if (s_override_hstdin)
            return s_override_hstdin;
        break;
    case STD_OUTPUT_HANDLE:
    case STD_ERROR_HANDLE:
        if (s_override_hstdout)
            return s_override_hstdout;
        break;
    }

    return GetStdHandle(n);
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
extern "C" void use_host_input_mode(HANDLE h, DWORD current_mode)
{
    assert(h && h != INVALID_HANDLE_VALUE);

    s_clink_input_mode = cleanup_console_input_mode(current_mode);

    if (s_host_input_mode != -1 && s_host_input_mode != current_mode)
    {
        SetConsoleMode(h, s_host_input_mode);
        debug_show_console_mode(nullptr, "host");
    }
}

//------------------------------------------------------------------------------
extern "C" void use_clink_input_mode(HANDLE h)
{
    assert(h && h != INVALID_HANDLE_VALUE);

    DWORD mode = 0;
    if (GetConsoleMode(h, &mode) && s_host_input_mode == -1)
        s_host_input_mode = mode;

    if (s_clink_input_mode != -1 && s_clink_input_mode != mode)
    {
        SetConsoleMode(h, s_clink_input_mode);
        debug_show_console_mode(nullptr, "clink");
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
        switch (get_current_ansi_handler())
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
        switch (get_current_ansi_handler())
        {
        case ansi_handler::conemu:
            mode |= ENABLE_MOUSE_INPUT;
            break;
        default:
            if (s_mouse_alt || s_mouse_ctrl || s_mouse_shift)
                mode |= ENABLE_MOUSE_INPUT;
            else if (get_current_ansi_handler() == ansi_handler::winterminal ||
                     get_current_ansi_handler() == ansi_handler::wezterm)
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
static int32 s_debug_console_mode = 0;
#ifdef DEBUG
int32 console_config::s_nested = 0;
#endif

//------------------------------------------------------------------------------
console_config::console_config(HANDLE handle, bool accept_mouse_input)
    : m_handle(handle ? handle : get_std_handle(STD_INPUT_HANDLE))
{
#ifdef DEBUG
    ++s_nested;
    assert(s_nested == 1);
#endif

    str<16> value;
    os::get_env("CLINK_DEBUG_CONSOLE_MODE", value);
    s_debug_console_mode = atoi(value.c_str());

    const BOOL is_console = GetConsoleMode(m_handle, &m_prev_mode);
    if (is_console)
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

    if (is_console && mode != m_prev_mode)
    {
        SetConsoleMode(m_handle, mode);
        debug_show_console_mode(&m_prev_mode, "config");
    }
}

//------------------------------------------------------------------------------
console_config::~console_config()
{
    DWORD mode = 0;
    GetConsoleMode(m_handle, &mode);

    if (mode != m_prev_mode)
    {
        SetConsoleMode(m_handle, m_prev_mode);
        debug_show_console_mode(nullptr, "~config");
    }

    g_accept_mouse_input = m_prev_accept_mouse_input;

#ifdef DEBUG
    --s_nested;
#endif
}

//------------------------------------------------------------------------------
void console_config::fix_quick_edit_mode(DWORD& mode)
{
    if (!g_accept_mouse_input)
        return;

    switch (get_current_ansi_handler())
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
static void append_mode(str_base& out, DWORD mode, bool bits=false)
{
    str<16> tmp;
    if (mode == -1 || mode <= 0xffff)
        tmp.format("%04x", mode);
    else
        tmp.format("%08x", mode);
    out.concat(tmp.c_str());

    static const struct { DWORD bit; const char* name; } c_bits[] =
    {
        { ENABLE_VIRTUAL_TERMINAL_INPUT,    "V" }, // 0x0200
        { ENABLE_AUTO_POSITION,             "A" }, // 0x0100
        { ENABLE_EXTENDED_FLAGS,            "X" }, // 0x0080
        { ENABLE_QUICK_EDIT_MODE,           "Q" }, // 0x0040
        { ENABLE_INSERT_MODE,               "I" }, // 0x0020
        { ENABLE_MOUSE_INPUT,               "M" }, // 0x0010
        { ENABLE_WINDOW_INPUT,              "W" }, // 0x0008
        { ENABLE_ECHO_INPUT,                "E" }, // 0x0004
        { ENABLE_LINE_INPUT,                "L" }, // 0x0002
        { ENABLE_PROCESSED_INPUT,           "P" }, // 0x0001
    };

    if (bits)
    {
        out.concat(" ");
        for (const auto& b : c_bits)
            out.concat((mode & b.bit) ? b.name : "-");
    }
}

//------------------------------------------------------------------------------
void debug_show_console_mode(const DWORD* prev_mode, const char* tag)
{
    str<> value;
    if (s_debug_console_mode)
        value.format("%d", s_debug_console_mode);
#ifdef DEBUG
    else
        os::get_env("DEBUG_CONSOLE_MODE", value);
#endif
    if (atoi(value.c_str()) != 0)
    {
        assert(g_printer);
        if (g_printer)
        {
            DWORD mode = 0;
            CONSOLE_SCREEN_BUFFER_INFO csbi = {};
            HANDLE hOut = get_std_handle(STD_OUTPUT_HANDLE);
            if (!GetConsoleMode(get_std_handle(STD_INPUT_HANDLE), &mode))
                return;
            if (!GetConsoleScreenBufferInfo(hOut, &csbi))
                return;

            const int32 row = atoi(value.c_str());
            const char* color = (row > 0) ? ";7" : ";7;90";
            str<> tmp;

            if (row > 0)
                value.format("\x1b[s\x1b[%uH\x1b[K", row);
            else if (csbi.dwCursorPosition.X > 0)
                value = "\n";
            else if (!g_printer->get_line_text(csbi.dwCursorPosition.Y, tmp) || tmp.length())
                value = "\n";

            tag = (row < 0) ? tag : nullptr;
            if (csbi.dwSize.X >= 40)
            {
                str<> rtext;
                if (tag)
                {
                    rtext.concat("\x1b[0;7;36m ");
                    rtext.concat(tag);
                    rtext.concat(" \x1b[m ");
                }
                rtext.concat("\x1b[0");
                rtext.concat(color);
                rtext.concat(";90m orig ");
                append_mode(rtext, s_host_input_mode, (csbi.dwSize.X >= 72));
                rtext.concat(" \x1b[m\x1b[G");

                const uint32 rlen = cell_count(rtext.c_str());
                tmp.format("\x1b[%uG", csbi.dwSize.X - rlen);
                value.concat(tmp.c_str());
                value.concat(rtext.c_str());
            }
            tmp.format("\x1b[0%sm qe %u \x1b[m \x1b[0%sm curr ", color, !!s_quick_edit, color);
            value.concat(tmp.c_str());
            append_mode(value, mode, (csbi.dwSize.X >= 72));
            value.concat(" \x1b[m");
            if (prev_mode)
            {
                tmp.format(" \x1b[0%s;90m prev ", color);
                value.concat(tmp.c_str());
                append_mode(value, *prev_mode);
                value.concat(" \x1b[m");
            }

            if (row > 0)
                value.concat("\x1b[u");
            else
                value.concat("\n");

            g_printer->print(value.c_str(), value.length());
        }
    }
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



//------------------------------------------------------------------------------
static thread_local int32 s_supersede_logging = 0;
suppress_implicit_write_console_logging::suppress_implicit_write_console_logging() { ++s_supersede_logging; }
suppress_implicit_write_console_logging::~suppress_implicit_write_console_logging() { --s_supersede_logging; }
bool suppress_implicit_write_console_logging::is_suppressed() { return s_supersede_logging > 0; }
