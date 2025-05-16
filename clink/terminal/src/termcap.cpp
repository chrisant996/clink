// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal_out.h"
#include "terminal_helpers.h"
#include "screen_buffer.h"

#include <core/base.h>
#include <core/str.h>
#include <core/settings.h>
#include <core/os.h>

#include <assert.h>

//------------------------------------------------------------------------------
extern setting_bool g_adjust_cursor_style;

//------------------------------------------------------------------------------
threadlocal static char gt_termcap_buffer[64];

//------------------------------------------------------------------------------
#define CSI(x) "\x1b[" #x
#define SS3(x) "\x1bO" #x
static const char c_default_term_ve[] = CSI(?12l) CSI(?25h);
static const char c_default_term_vs[] = CSI(?12;25h);
static const char c_default_term_vb[] = "\x1bg";
static wstr_moveable s_term_ve;
static wstr_moveable s_term_vs;
bool g_enhanced_cursor = false;

//------------------------------------------------------------------------------
static bool is_cursor_blink_code(const wchar_t* chars)
{
    return (wcscmp(chars, L"\u001b[?12l") == 0 ||
            wcscmp(chars, L"\u001b[?12h") == 0);
}

//------------------------------------------------------------------------------
extern "C" int32 show_cursor(int32 visible)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    if (visible)
    {
        const wchar_t* str = g_enhanced_cursor ? s_term_vs.c_str() : s_term_ve.c_str();
        uint32 len = g_enhanced_cursor ? s_term_vs.length() : s_term_ve.length();

        // Windows Terminal doesn't support using SetConsoleCursorInfo to change
        // the cursor size, so use termcap strings instead.
        if (get_native_ansi_handler() == ansi_handler::winterminal ||
            get_native_ansi_handler() == ansi_handler::wezterm)
        {
            if (!str[0])
            {
                str = g_enhanced_cursor ? L"\u001b[1 q" : L"\u001b[0 q";
                len = 5;
            }
        }

        // If there's a termcap string and it starts with ESC, write it.
        const wchar_t c = str[0];
        if (c == '\x1b')
        {
            DWORD dw;
            WriteConsoleW(h, str, len, &dw, nullptr);

            // If the termcap string is not a blink code, proceed to the common
            // show/hide logic to ensure the cursor is visible.  If the termcap
            // string is a blink code, proceed to the default show logic to set
            // both the style and visibility as usual.
            if (!is_cursor_blink_code(str))
                goto common;
        }

        // Set cursor style and visibility using default console APIs.  This
        // doesn't work well on Windows Terminal, per notes above.
        return cursor_style(h, g_enhanced_cursor, 1);
    }

common:

    // On Windows terminal, the common show/hide logic is simply escape codes.
    if (get_native_ansi_handler() >= ansi_handler::winterminal)
    {
        DWORD dw;
        const int32 was_visible = cursor_style(h, -1, -1);
        WriteConsoleW(h, visible ? L"\u001b[?25h" : L"\u001b[?25l", 6, &dw, nullptr);
        return was_visible;
    }

    // Use default console APIs to set the visibility.
    return cursor_style(h, -1, !!visible);
}

//------------------------------------------------------------------------------
static wchar_t from_hex(wchar_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return 0;
}

//------------------------------------------------------------------------------
static void fetch_term_string(const char* envvar, wstr_moveable& out)
{
    str<> tmp;
    if (!os::get_env(envvar, tmp))
    {
        out.clear();
        return;
    }

    wstr_moveable wtmp(tmp.c_str());
    if (wcschr(wtmp.c_str(), '\\'))
    {
        out.clear();
        for (const wchar_t* s = wtmp.c_str(); *s; s++)
        {
            if (*s != '\\')
                out.concat(s, 1);
            else
            {
                s++;
                if (*s == 'e')
                    out.concat(L"\u001b", 1);
                else if (*s == 0)
                    break;
                else if (*s != 'x')
                    out.concat(s, 1);
                else
                {
                    wchar_t c = 0;
                    if (!s[1] || !s[2])
                        break;
                    c = from_hex(s[1]) << 4;
                    c += from_hex(s[2]);
                    out.concat(&c, 1);
                    s += 2;
                }
            }
        }
    }
    else
    {
        out = std::move(wtmp);
    }
}

//------------------------------------------------------------------------------
void terminal_out::init_termcap_intercept()
{
    fetch_term_string("CLINK_TERM_VE", s_term_ve);
    fetch_term_string("CLINK_TERM_VS", s_term_vs);
}

//------------------------------------------------------------------------------
// Returns:
//  - false = not intercepted; process normally.
//  - true  = intercepted and handled.
bool terminal_out::do_termcap_intercept(const char* chars)
{
    // If it's the 've' or 'vs' termcap string and there's a custom string then
    // use the custom string.  And if the custom string is exactly and only a
    // cursor blink code then continue so that Clink sets the cursor shape as
    // usual.  So that you can e.g. make insert mode use a blinking Legacy Style
    // cursor, and make overwrite mode use a non-blinking solid box.
    //
    // These termcap overrides are intended to allow the user more control over
    // cursor style in Clink, e.g. by using DECSCUSR codes.
    //
    // CSI Ps SP q
    //              Set cursor style (DECSCUSR, VT520).
    //              Ps = 0  -> blinking block.
    //              Ps = 1  -> blinking block (default).
    //              Ps = 2  -> steady block.
    //              Ps = 3  -> blinking underline.
    //              Ps = 4  -> steady underline.
    //              Ps = 5  -> blinking bar (xterm).
    //              Ps = 6  -> steady bar (xterm).
    //
    //              Note:  Windows Terminal redefined Ps = 0 as follows:
    //              Ps = 0  -> default cursor shape configured by the user.

    if (chars == c_default_term_ve)
    {
        g_enhanced_cursor = false;
        show_cursor(true);
    }
    else if (chars == c_default_term_vs)
    {
        g_enhanced_cursor = true;
        show_cursor(true);
    }
    else if (chars == c_default_term_vb)
        visible_bell();
    else
        return false;

    return true;
}

//------------------------------------------------------------------------------
void terminal_out::visible_bell()
{
    if (!g_adjust_cursor_style.get())
        return;

    const bool enhanced = g_enhanced_cursor;
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

    // Remember the cursor visibility.
    int32 was_visible = cursor_style(handle, -1, -1);

    // Use the opposite cursor style from whatever is currently active.
    if (enhanced)
        write(c_default_term_ve);
    else
        write(c_default_term_vs);

    // Not sure why the cursor position gets refreshed here.  Maybe it resets
    // the blink timer?
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(handle, &csbi);
    COORD xy = { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y };
    SetConsoleCursorPosition(handle, xy);

    // Sleep briefly so the alternate cursor shape can be seen.
    Sleep(20);

    // Restore the previous cursor style.  Also shows the cursor.
    if (enhanced)
        write(c_default_term_vs);
    else
        write(c_default_term_ve);

    // If the cursor was not previously visible, hide it.
    if (!was_visible)
        show_cursor(was_visible);

    assert(enhanced == g_enhanced_cursor);
}



//------------------------------------------------------------------------------
static int32 get_cap(const char* name)
{
    int32 a = int32(*name);
    int32 b = a ? int32(name[1]) : 0;
    return (a << 8) | b;
}

//------------------------------------------------------------------------------
static void get_screen_size(int32& width, int32& height)
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle && handle != INVALID_HANDLE_VALUE)
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(handle, &csbi))
        {
            width = csbi.dwSize.X;
            height = (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
            return;
        }
    }

    width = 80;
    height = 25;
}

#if defined(__cplusplus)
extern "C" {
#endif

//------------------------------------------------------------------------------
int32 _rl_output_character_function(int32); // Terminal can't #include from Readline.

//------------------------------------------------------------------------------
int32 tputs(const char* str, int32 affcnt, int32 (*putc_func)(int32))
{
    extern int32 hooked_fwrite(const void* data, int32 size, int32 count, FILE* stream);
    extern FILE *_rl_out_stream;

    if (putc_func == _rl_output_character_function)
    {
        hooked_fwrite(str, (int32)strlen(str), 1, _rl_out_stream);
        return 0;
    }

    if (str != nullptr)
        while (*str)
            putc_func(*str++);

    return 0;
}

//------------------------------------------------------------------------------
int32 tgetent(char* bp, const char* name)
{
    *bp = '\0';
    return 1;
}

//------------------------------------------------------------------------------
int32 tgetnum(char* name)
{
    int32 width, height;
    int32 cap = get_cap(name);

    get_screen_size(width, height);

    switch (cap)
    {
    case 'co': return width;
    case 'li': return height;
    }

    return 0;
}

//------------------------------------------------------------------------------
int32 tgetflag(char* name)
{
    int32 cap = get_cap(name);

    switch (cap)
    {
    case 'am':  return 1;
    case 'km':  return 1;
    case 'xn':  return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
char* tgetstr(const char* name, char** out)
{
    int32 cap = get_cap(name);
    const char* str = nullptr;
    switch (cap)
    {
    // Insert and delete N and single characters.
    case 'dc': str = CSI(P);   break;
    case 'DC': str = CSI(%dP); break;
    case 'ic': str = CSI(@);   break;
    case 'IC': str = CSI(%d@); break;

    // Clear lines and screens.
    case 'cb': str = CSI(1K);       break; // Line to cursor
    case 'ce': str = CSI(K);        break; // Line to end
    case 'cd': str = CSI(J);        break; // Screen to end
    case 'cl': str = CSI(H) CSI(J); break; // Clear screen, cursor to top-left.

    // Movement key bindings.
    case 'kh': str = CSI(H);  break; // Home
    case '@7': str = CSI(F);  break; // End
    case 'kD': str = CSI(3~); break; // Del
    case 'kI': str = CSI(2~); break; // Ins
    case 'ku': str = CSI(A);  break; // Up
    case 'kd': str = CSI(B);  break; // Down
    case 'kr': str = CSI(C);  break; // Right
    case 'kl': str = CSI(D);  break; // Left

    // Cursor movement.
    case 'ch': str = CSI(%dG); break;
    case 'cr': str = "\x0d"; break;
    case 'le': str = "\x08"; break;
    case 'LE': str = CSI(%dD); break;
    case 'nd': str = CSI(C); break;
    case 'ND': str = CSI(%dC); break;
    case 'up': str = CSI(A); break;
    case 'UP': str = CSI(%dA); break;

    // Saved cursor position.
    case 'sc': str = CSI(s); break;
    case 'rc': str = CSI(u); break;

    // Cursor style.
    case 've': str = c_default_term_ve; break;
    case 'vs': str = c_default_term_vs; break;

    // Visual bell.
    case 'vb': str = c_default_term_vb; break;
    }

    if (str != nullptr && out != nullptr && *out != nullptr)
    {
        strcpy(*out, str);
        *out += strlen(str) + 1;
    }

    return const_cast<char*>(str);

#undef SS3
#undef CSI
}

//------------------------------------------------------------------------------
char* tgoto(const char* base, int32 x, int32 y)
{
    str_base(gt_termcap_buffer).format(base, y);
    return gt_termcap_buffer;
}

#if defined(__cplusplus)
} // extern "C"
#endif
