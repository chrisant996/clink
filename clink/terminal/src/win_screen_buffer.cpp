// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_screen_buffer.h"
#include "cielab.h"
#include "find_line.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/str_iter.h>
#include <core/debugheap.h>

#include <assert.h>

#include <regex>

// For compatibility with Windows 8.1 SDK.
#if !defined( ENABLE_VIRTUAL_TERMINAL_PROCESSING )
# define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x0004
#elif ENABLE_VIRTUAL_TERMINAL_PROCESSING != 0x0004
# error ENABLE_VIRTUAL_TERMINAL_PROCESSING must be 0x0004
#endif

//------------------------------------------------------------------------------
static ansi_handler s_native_ansi_handler = ansi_handler::unknown;
static ansi_handler s_current_ansi_handler = ansi_handler::unknown;
static const char* s_consolez_dll = nullptr;
static const char* s_found_what = nullptr;
bool g_color_emoji = false; // Global for performance, since it's accessed in tight loops.

ansi_handler get_native_ansi_handler()
{
    return s_native_ansi_handler;
}

ansi_handler get_current_ansi_handler()
{
    return s_current_ansi_handler;
}

//------------------------------------------------------------------------------
static const char* s_conemu_dll = nullptr;
bool is_conemu()
{
    return !!s_conemu_dll;
}

//------------------------------------------------------------------------------
static const char* const conemu_dll_names[] =
{
    "ConEmuHk.dll",
    "ConEmuHk32.dll",
    "ConEmuHk64.dll",
    nullptr
};
static const char* const ansicon_dll_names[] =
{
    "ANSI.dll",
    "ANSI32.dll",
    "ANSI64.dll",
    nullptr
};
static const char* const consolez_dll_names[] =
{
    "ConsoleHook.dll",
    nullptr
};

//------------------------------------------------------------------------------
static const char* is_dll_loaded(const char* const* dll_names)
{
    while (*dll_names)
    {
        if (GetModuleHandle(*dll_names) != NULL)
            return *dll_names;
        dll_names++;
    }

    return nullptr;
}



//------------------------------------------------------------------------------
static setting_enum g_terminal_emulation(
    "terminal.emulation",
    "Controls VT emulation",
    "Clink can emulate Virtual Terminal processing if the console doesn't\n"
    "natively.  When set to 'emulate' then Clink performs VT emulation and handles\n"
    "ANSI escape codes.  When 'native' then Clink passes output directly to the\n"
    "console.  Or when 'auto' then Clink performs VT emulation unless native\n"
    "terminal support is detected (such as when hosted inside ConEmu, Windows\n"
    "Terminal, WezTerm, or Windows 10 new console, or when using ANSICON).",
    "native,emulate,auto",
    2);

static setting_enum g_terminal_color_emoji(
    "terminal.color_emoji",
    "Color emoji support in terminal",
    "Set this to indicate whether the terminal program draws emoji using colored\n"
    "double width characters.  This needs to be set accurately in order for Clink\n"
    "to display the input line properly when it contains emoji characters.\n"
    "When set to 'off' Clink assumes emoji are rendered using 1 character cell.\n"
    "When set to 'on' Clink assumes emoji are rendered using 2 character cells.\n"
    "When set to 'auto' (the default) Clink assumes emoji are rendered using 2\n"
    "character cells when using Windows Terminal or WezTerm, or otherwise using 1\n"
    "character cell.",
    "off,on,auto",
    2);

//------------------------------------------------------------------------------
win_screen_buffer::~win_screen_buffer()
{
    close();
    free(m_attrs);
    free(m_chars);
}

//------------------------------------------------------------------------------
void win_screen_buffer::open()
{
    assert(!m_handle);
    m_handle = GetStdHandle(STD_OUTPUT_HANDLE);
}

//------------------------------------------------------------------------------
void win_screen_buffer::begin()
{
    if (!m_handle)
        open();

    m_ready++;
    if (m_ready > 1)
        return;

    static bool detect_native_ansi_handler = true;
    if (detect_native_ansi_handler)
    {
        detect_native_ansi_handler = false;

        do
        {
            // Check for ConEmu.
            s_conemu_dll = is_dll_loaded(conemu_dll_names);
            if (s_conemu_dll)
            {
                s_found_what = s_conemu_dll;
                s_native_ansi_handler = ansi_handler::conemu;
                break;
            }

            // Check for Windows Terminal.
            str<16> wt_session;
            if (os::get_env("WT_SESSION", wt_session))
            {
                s_found_what = "WT_SESSION";
                s_native_ansi_handler = ansi_handler::winterminal;
                break;
            }

            // Check for WezTerm.
            str<16> wez;
            if (os::get_env("WEZTERM_EXECUTABLE", wez) &&
                os::get_env("WEZTERM_PANE", wez))
            {
                s_found_what = "WEZTERM_EXECUTABLE and WEZTERM_PANE";
                s_native_ansi_handler = ansi_handler::wezterm;
                break;
            }

            // Check for Ansi dlls loaded.
            const char* foundwhat = is_dll_loaded(ansicon_dll_names);
            if (foundwhat)
            {
                s_found_what = foundwhat;
                s_native_ansi_handler = ansi_handler::ansicon;
                break;
            }

            // Check for native virtual terminal support in Windows.
#pragma warning(push)
#pragma warning(disable:4996)
            OSVERSIONINFO ver = { sizeof(ver) };
            if (GetVersionEx(&ver) && ver.dwBuildNumber >= 15063)
            {
                DWORD type;
                DWORD data;
                DWORD size;
                LSTATUS status = RegGetValue(HKEY_CURRENT_USER, "Console", "ForceV2", RRF_RT_REG_DWORD, &type, &data, &size);
                if (status != ERROR_SUCCESS ||
                    type != REG_DWORD ||
                    size != sizeof(data) ||
                    data != 0)
                {
                    s_found_what = "Windows build >= 15063, console V2";
                    s_native_ansi_handler = ansi_handler::winconsolev2;
                    // DON'T BREAK; CONTINUE DETECTING -- because ConsoleZ
                    // doesn't provide ANSI handling, but it also defeats
                    // ConsoleV2 ANSI handling.
                }
            }
#pragma warning(pop)

            // Check for ConsoleZ dlls loaded.
            s_consolez_dll = is_dll_loaded(consolez_dll_names);
            if (s_consolez_dll && s_native_ansi_handler == ansi_handler::winconsolev2)
            {
                // Downgrade to basic support since ConsoleZ doesn't support
                // 256 color or 24 bit color.
                s_found_what = s_consolez_dll;
                s_native_ansi_handler = ansi_handler::winconsole;
                break;
            }
        }
        while (false);
    }

    GetConsoleMode(m_handle, &m_prev_mode);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    m_default_attr = csbi.wAttributes & attr_mask_all;
    m_bold = !!(m_default_attr & attr_mask_bold);
    m_reverse = false;

    bool native_vt = m_native_vt;
    switch (g_terminal_emulation.get())
    {
    case 0:
        native_vt = true;
        s_current_ansi_handler = s_native_ansi_handler;
        break;

    case 1:
        native_vt = false;
        s_current_ansi_handler = ansi_handler::clink;
        break;

    case 2:
        native_vt = s_native_ansi_handler >= ansi_handler::first_native;
        s_current_ansi_handler = native_vt ? s_native_ansi_handler : ansi_handler::clink;
        break;
    }

    if (m_native_vt != native_vt)
    {
        if (!native_vt)
            LOG("Using emulated terminal support.");
        else if (s_found_what)
            LOG("Using native terminal support; found '%s'.", s_found_what);
        else
            LOG("Using native terminal support.");
    }

    m_native_vt = native_vt;

    if (m_native_vt)
        SetConsoleMode(m_handle, m_prev_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    switch (g_terminal_color_emoji.get())
    {
    case 0: g_color_emoji = false; break;
    case 1: g_color_emoji = true; break;
    case 2: g_color_emoji = (s_native_ansi_handler == ansi_handler::winterminal ||
                             s_native_ansi_handler == ansi_handler::wezterm); break;
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::end()
{
    if (m_ready > 0)
    {
        m_ready--;
        if (!m_ready)
        {
            SetConsoleTextAttribute(m_handle, m_default_attr);
            SetConsoleMode(m_handle, m_prev_mode);
        }
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::close()
{
    m_handle = nullptr;
}

//------------------------------------------------------------------------------
void win_screen_buffer::write(const char* data, int length)
{
    assert(m_ready);

    str_iter iter(data, length);
    while (length > 0)
    {
        wchar_t wbuf[384];
        int n = min<int>(sizeof_array(wbuf), length + 1);
        n = to_utf16(wbuf, n, iter);
        if (!n && !*iter.get_pointer())
        {
            assert(false); // Very inefficient, and shouldn't be possible.
            wbuf[0] = '\0';
            n = 1;
        }

        DWORD written;
        WriteConsoleW(m_handle, wbuf, n, &written, nullptr);

        n = int(iter.get_pointer() - data);
        length -= n;
        data += n;
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::flush()
{
    // When writing to the console conhost.exe will restart the cursor blink
    // timer and hide it which can be disorientating, especially when moving
    // around a line. The below will make sure it stays visible.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
int win_screen_buffer::get_columns() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    return csbi.dwSize.X;
}

//------------------------------------------------------------------------------
int win_screen_buffer::get_rows() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    return (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::get_line_text(int line, str_base& out) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return false;

    if (!ensure_chars_buffer(csbi.dwSize.X))
        return false;

    COORD coord = { 0, SHORT(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputCharacterW(m_handle, m_chars, csbi.dwSize.X, coord, &len))
        return false;
    if (len != csbi.dwSize.X)
        return false;

    while (len > 0 && iswspace(m_chars[len - 1]))
        len--;

    out.clear();
    wstr_iter tmpi(m_chars, len);
    to_utf8(out, tmpi);
    return true;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::has_native_vt_processing() const
{
    return m_native_vt;
}

//------------------------------------------------------------------------------
void win_screen_buffer::clear(clear_type type)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    int width, height, count = 0;
    COORD xy;

    switch (type)
    {
    case clear_type_all:
        width = csbi.dwSize.X;
        height = (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
        xy = { 0, csbi.srWindow.Top };
        break;

    case clear_type_before:
        width = csbi.dwSize.X;
        height = csbi.dwCursorPosition.Y - csbi.srWindow.Top;
        xy = { 0, csbi.srWindow.Top };
        count = csbi.dwCursorPosition.X + 1;
        break;

    case clear_type_after:
        width = csbi.dwSize.X;
        height = csbi.srWindow.Bottom - csbi.dwCursorPosition.Y;
        xy = { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y };
        count = width - csbi.dwCursorPosition.X;
        break;
    }

    count += width * height;

    DWORD written;
    FillConsoleOutputCharacterW(m_handle, ' ', count, xy, &written);
    FillConsoleOutputAttribute(m_handle, csbi.wAttributes, count, xy, &written);
}

//------------------------------------------------------------------------------
void win_screen_buffer::clear_line(clear_type type)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    int width;
    COORD xy;
    switch (type)
    {
    case clear_type_all:
        width = csbi.dwSize.X;
        xy = { 0, csbi.dwCursorPosition.Y };
        break;

    case clear_type_before:
        width = csbi.dwCursorPosition.X + 1;
        xy = { 0, csbi.dwCursorPosition.Y };
        break;

    case clear_type_after:
        width = csbi.dwSize.X - csbi.dwCursorPosition.X;
        xy = { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y };
        break;
    }

    DWORD written;
    FillConsoleOutputCharacterW(m_handle, ' ', width, xy, &written);
    FillConsoleOutputAttribute(m_handle, csbi.wAttributes, width, xy, &written);
}

//------------------------------------------------------------------------------
void win_screen_buffer::set_horiz_cursor(int column)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    const SMALL_RECT& window = csbi.srWindow;
    int width = (window.Right - window.Left) + 1;
    int height = (window.Bottom - window.Top) + 1;

    column = clamp(column, 0, width - 1);

    COORD xy = { static_cast<SHORT>(window.Left + column), csbi.dwCursorPosition.Y };
    SetConsoleCursorPosition(m_handle, xy);
}

//------------------------------------------------------------------------------
void win_screen_buffer::set_cursor(int column, int row)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    const SMALL_RECT& window = csbi.srWindow;
    int width = (window.Right - window.Left) + 1;
    int height = (window.Bottom - window.Top) + 1;

    column = clamp(column, 0, width - 1);
    row = clamp(row, 0, height - 1);

    COORD xy = { static_cast<SHORT>(window.Left + column), static_cast<SHORT>(window.Top + row) };
    SetConsoleCursorPosition(m_handle, xy);
}

//------------------------------------------------------------------------------
void win_screen_buffer::move_cursor(int dx, int dy)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    COORD xy = {
        SHORT(clamp(csbi.dwCursorPosition.X + dx, 0, csbi.dwSize.X - 1)),
        SHORT(clamp(csbi.dwCursorPosition.Y + dy, 0, csbi.dwSize.Y - 1)),
    };
    SetConsoleCursorPosition(m_handle, xy);
}

//------------------------------------------------------------------------------
void win_screen_buffer::save_cursor()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    const SMALL_RECT& window = csbi.srWindow;
    int width = (window.Right - window.Left) + 1;
    int height = (window.Bottom - window.Top) + 1;

    m_saved_cursor = {
        SHORT(clamp(csbi.dwCursorPosition.X - window.Left, 0, width)),
        SHORT(clamp(csbi.dwCursorPosition.Y - window.Top, 0, height)),
    };
}

//------------------------------------------------------------------------------
void win_screen_buffer::restore_cursor()
{
    set_cursor(m_saved_cursor.X, m_saved_cursor.Y);
}

//------------------------------------------------------------------------------
void win_screen_buffer::insert_chars(int count)
{
    if (count <= 0)
        return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    SMALL_RECT rect;
    rect.Left = csbi.dwCursorPosition.X;
    rect.Right = csbi.dwSize.X;
    rect.Top = rect.Bottom = csbi.dwCursorPosition.Y;

    CHAR_INFO fill;
    fill.Char.AsciiChar = ' ';
    fill.Attributes = csbi.wAttributes;

    csbi.dwCursorPosition.X += count;

    ScrollConsoleScreenBuffer(m_handle, &rect, NULL, csbi.dwCursorPosition, &fill);
}

//------------------------------------------------------------------------------
void win_screen_buffer::delete_chars(int count)
{
    if (count <= 0)
        return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    SMALL_RECT rect;
    rect.Left = csbi.dwCursorPosition.X + count;
    rect.Right = csbi.dwSize.X - 1;
    rect.Top = rect.Bottom = csbi.dwCursorPosition.Y;

    CHAR_INFO fill;
    fill.Char.AsciiChar = ' ';
    fill.Attributes = csbi.wAttributes;

    ScrollConsoleScreenBuffer(m_handle, &rect, NULL, csbi.dwCursorPosition, &fill);

    int chars_moved = rect.Right - rect.Left + 1;
    if (chars_moved < count)
    {
        COORD xy = csbi.dwCursorPosition;
        xy.X += chars_moved;

        count -= chars_moved;

        DWORD written;
        FillConsoleOutputCharacterW(m_handle, ' ', count, xy, &written);
        FillConsoleOutputAttribute(m_handle, csbi.wAttributes, count, xy, &written);
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::set_attributes(attributes attr)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    int out_attr = csbi.wAttributes & attr_mask_all;

    // Un-reverse so processing can operate on normalized attributes.
    if (m_reverse)
    {
        int fg = (out_attr & ~attr_mask_bg);
        int bg = (out_attr & attr_mask_bg);
        out_attr = (fg << 4) | (bg >> 4);
    }

    // Note to self; lookup table is probably much faster.
    auto swizzle = [] (int rgbi) {
        int b_r_ = ((rgbi & 0x01) << 2) | !!(rgbi & 0x04);
        return (rgbi & 0x0a) | b_r_;
    };

    // Map RGB/XTerm256 colors
    if (!get_nearest_color(attr))
        return;

    // Bold
    bool apply_bold = false;
    if (auto bold_attr = attr.get_bold())
    {
        m_bold = !!(bold_attr.value);
        apply_bold = true;
    }

    // Underline
    if (auto underline = attr.get_underline())
    {
        if (underline.value)
            out_attr |= attr_mask_underline;
        else
            out_attr &= ~attr_mask_underline;
    }

    // Foreground color
    bool bold = m_bold;
    if (auto fg = attr.get_fg())
    {
        int value = fg.is_default ? m_default_attr : swizzle(fg->value);
        value &= attr_mask_fg;
        out_attr = (out_attr & ~attr_mask_fg) | value;
        bold |= (value > 7);
    }
    else
        bold |= (out_attr & attr_mask_bold) != 0;

    // Adjust intensity per bold.  Bold can add intensity.  Nobold can remove
    // intensity added by bold, but cannot remove intensity built into the
    // color number.
    //
    // In other words:
    //  - If the color is 36 (cyan) then bold can make it bright cyan.
    //  - If the color is 36 (cyan) then nobold has no visible effect.
    //  - If the color is 1;36 (bold cyan) then nobold can make it cyan.
    //  - If the color is 96 (bright cyan) then bold has no visible effect (but
    //    some terminal implementations apply a bold font with bright cyan as
    //    the color).
    //  - If the color is 96 (bright cyan) then nobold has no visible effect.
    //  - If the color is 1;96 (bold bright cyan) then nobold has no visible
    //    effect (but some terminal implementations apply a non-bold font with
    //    bright cyan as the color).
    if (apply_bold)
    {
        if (bold)
            out_attr |= attr_mask_bold;
        else
            out_attr &= ~attr_mask_bold;
    }

    // Background color
    if (auto bg = attr.get_bg())
    {
        int value = bg.is_default ? m_default_attr : (swizzle(bg->value) << 4);
        out_attr = (out_attr & ~attr_mask_bg) | (value & attr_mask_bg);
    }

    // Reverse video
    if (auto rev = attr.get_reverse())
        m_reverse = rev.value;

    // Apply reverse video
    if (m_reverse)
    {
        int fg = (out_attr & ~attr_mask_bg);
        int bg = (out_attr & attr_mask_bg);
        out_attr = (fg << 4) | (bg >> 4);
    }

    out_attr |= csbi.wAttributes & ~attr_mask_all;
    SetConsoleTextAttribute(m_handle, short(out_attr));
}

//------------------------------------------------------------------------------
static bool get_nearest_color(void* handle, const unsigned char (&rgb)[3], unsigned char& attr)
{
    static HMODULE hmod = GetModuleHandle("kernel32.dll");
    static FARPROC proc = GetProcAddress(hmod, "GetConsoleScreenBufferInfoEx");
    typedef BOOL (WINAPI* GCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);

    if (!proc)
        return false;

    CONSOLE_SCREEN_BUFFER_INFOEX infoex = { sizeof(infoex) };
    if (!GCSBIEx(proc)(handle, &infoex))
        return false;

    cie::lab target(RGB(rgb[0], rgb[1], rgb[2]));
    double best_deltaE = 0;
    int best_idx = -1;

    for (int i = sizeof_array(infoex.ColorTable); i--;)
    {
        cie::lab candidate(infoex.ColorTable[i]);
        double deltaE = cie::deltaE_2(target, candidate);
        if (best_idx < 0 || best_deltaE > deltaE)
        {
            best_deltaE = deltaE;
            best_idx = i;
        }
    }

    if (best_idx < 0)
        return false;

    static const int dos_to_ansi_order[] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    attr = (best_idx & 0x08) + dos_to_ansi_order[best_idx & 0x07];
    return true;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::get_nearest_color(attributes& attr) const
{
    const attributes::color fg = attr.get_fg().value;
    const attributes::color bg = attr.get_bg().value;
    if (fg.is_rgb)
    {
        unsigned char val;
        unsigned char rgb[3];
        fg.as_888(rgb);
        if (!::get_nearest_color(m_handle, rgb, val))
            return false;
        attr.set_fg(val);
    }
    if (bg.is_rgb)
    {
        unsigned char val;
        unsigned char rgb[3];
        bg.as_888(rgb);
        if (!::get_nearest_color(m_handle, rgb, val))
            return false;
        attr.set_bg(val);
    }
    return true;
}

//------------------------------------------------------------------------------
int win_screen_buffer::is_line_default_color(int line) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return -1;

    if (!ensure_attrs_buffer(csbi.dwSize.X))
        return -1;

    int ret = true;
    COORD coord = { 0, SHORT(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputAttribute(m_handle, m_attrs, csbi.dwSize.X, coord, &len))
        return -1;
    if (len != csbi.dwSize.X)
        return -1;

    for (const WORD* attr = m_attrs; len--; attr++)
        if (*attr != m_default_attr)
            return false;

    return true;
}

//------------------------------------------------------------------------------
int win_screen_buffer::line_has_color(int line, const BYTE* attrs, int num_attrs, BYTE mask) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return -1;

    if (!ensure_attrs_buffer(csbi.dwSize.X))
        return -1;

    int ret = true;
    COORD coord = { 0, SHORT(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputAttribute(m_handle, m_attrs, csbi.dwSize.X, coord, &len))
        return -1;
    if (len != csbi.dwSize.X)
        return -1;

    const BYTE* end_attrs = attrs + num_attrs;
    for (const WORD* attr = m_attrs; len--; attr++)
    {
        for (const BYTE* find_attr = attrs; find_attr < end_attrs; find_attr++)
            if ((BYTE(*attr) & mask) == (*find_attr & mask))
                return true;
    }

    return false;
}

//------------------------------------------------------------------------------
int win_screen_buffer::find_line(int starting_line, int distance, const char* text, find_line_mode mode, const BYTE* attrs, int num_attrs, BYTE mask) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return -2;

    if (text && !ensure_chars_buffer(csbi.dwSize.X))
        return -2;
    if (attrs && num_attrs > 0 && !ensure_attrs_buffer(csbi.dwSize.X))
        return -2;

    return ::find_line(m_handle, csbi,
                       m_chars, m_chars_capacity,
                       m_attrs, m_attrs_capacity,
                       starting_line, distance,
                       text, mode,
                       attrs, num_attrs, mask);
}

//------------------------------------------------------------------------------
bool win_screen_buffer::ensure_chars_buffer(int width) const
{
    if (width > m_chars_capacity)
    {
        WCHAR* chars = static_cast<WCHAR*>(realloc(m_chars, (width + 1) * sizeof(*m_chars)));
        if (!chars)
            return false;
        m_chars = chars;
        m_chars_capacity = width;
#ifdef USE_MEMORY_TRACKING
        dbgsetignore(chars, 1);
        dbgsetlabel(chars, "win_screen_buffer::m_chars", false);
#endif
    }
    return true;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::ensure_attrs_buffer(int width) const
{
    if (width > m_attrs_capacity)
    {
        m_attrs = static_cast<WORD*>(malloc((width + 1) * sizeof(*m_attrs)));
        if (!m_attrs)
            return false;
        m_attrs_capacity = width;
    }
    return true;
}
