// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal_out.h"
#include "terminal_helpers.h"

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
void terminal_out::init_termcap_intercept()
{
    str<> tmp;
    if (os::get_env("CLINK_TERM_VE", tmp))
        s_term_ve = tmp.c_str();
    else
        s_term_ve.clear();
    if (os::get_env("CLINK_TERM_VS", tmp))
        s_term_vs = tmp.c_str();
    else
        s_term_vs.clear();
}

//------------------------------------------------------------------------------
// Returns:
//      0   = not intercepted; process normally.
//      1   = intercepted and handled.
//      -1  = caller must intercept.
int terminal_out::do_termcap_intercept(const char* chars)
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
        const wchar_t c = s_term_ve[0];
        g_enhanced_cursor = false;
        if (c == '\x1b')
        {
            DWORD dw;
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            WriteConsoleW(h, s_term_ve.c_str(), s_term_ve.length(), &dw, nullptr);
            cursor_style(h, -1, 1);
            if (!is_cursor_blink_code(s_term_ve.c_str()))
                return 1;
        }
        else if (c)
            return 1;
        return -1;
    }
    else if (chars == c_default_term_vs)
    {
        const wchar_t c = s_term_vs[0];
        g_enhanced_cursor = true;
        if (c == '\x1b')
        {
            DWORD dw;
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            WriteConsoleW(h, s_term_vs.c_str(), s_term_vs.length(), &dw, nullptr);
            cursor_style(h, -1, 1);
            if (!is_cursor_blink_code(s_term_vs.c_str()))
                return 1;
        }
        if (c)
            return 1;
        return -1;
    }
    else if (chars == c_default_term_vb)
    {
        visible_bell();
        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
void terminal_out::visible_bell()
{
    if (!g_adjust_cursor_style.get())
        return;

    const bool enhanced = g_enhanced_cursor;
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

    // Remember the cursor visibility.
    int was_visible = cursor_style(handle, -1, -1);

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

    // Restore the previous cursor style.
    if (enhanced)
        write(c_default_term_vs);
    else
        write(c_default_term_ve);

    // Restore the previous cursor visibility.
    cursor_style(handle, -1, was_visible);

    assert(enhanced == g_enhanced_cursor);
}



//------------------------------------------------------------------------------
static int get_cap(const char* name)
{
    int a = int(*name);
    int b = a ? int(name[1]) : 0;
    return (a << 8) | b;
}

//------------------------------------------------------------------------------
static void get_screen_size(int& width, int& height)
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle != INVALID_HANDLE_VALUE)
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
int _rl_output_character_function(int); // Terminal can't #include from Readline.

//------------------------------------------------------------------------------
int tputs(const char* str, int affcnt, int (*putc_func)(int))
{
    extern int hooked_fwrite(const void* data, int size, int count, FILE* stream);
    extern FILE *_rl_out_stream;

    if (putc_func == _rl_output_character_function)
    {
        hooked_fwrite(str, (int)strlen(str), 1, _rl_out_stream);
        return 0;
    }

    if (str != nullptr)
        while (*str)
            putc_func(*str++);

    return 0;
}

//------------------------------------------------------------------------------
int tgetent(char* bp, const char* name)
{
    *bp = '\0';
    return 1;
}

//------------------------------------------------------------------------------
int tgetnum(char* name)
{
    int width, height;
    int cap = get_cap(name);

    get_screen_size(width, height);

    switch (cap)
    {
    case 'co': return width;
    case 'li': return height;
    }

    return 0;
}

//------------------------------------------------------------------------------
int tgetflag(char* name)
{
    int cap = get_cap(name);

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
    int cap = get_cap(name);
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
    case 'kh': str = CSI(H); break; // Home
    case '@7': str = CSI(F); break; // End
    case 'kD': str = CSI(3); break; // Del
    case 'kI': str = CSI(2); break; // Ins
    case 'ku': str = CSI(A); break; // Up
    case 'kd': str = CSI(B); break; // Down
    case 'kr': str = CSI(C); break; // Right
    case 'kl': str = CSI(D); break; // Left

    // Cursor movement.
    case 'ch': str = CSI(%dG); break;
    case 'cr': str = "\x0d"; break;
    case 'le': str = "\x08"; break;
    case 'nd': str = CSI(C); break;
    case 'up': str = CSI(A); break;

    // Cursor style
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
char* tgoto(char* base, int x, int y)
{
    str_base(gt_termcap_buffer).format(base, y);
    return gt_termcap_buffer;
}

#if defined(__cplusplus)
} // extern "C"
#endif
