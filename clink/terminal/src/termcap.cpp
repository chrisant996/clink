// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/str.h>

#include <Windows.h>

//------------------------------------------------------------------------------
threadlocal static char gt_termcap_buffer[64];

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
int tputs(const char* str, int count, int (*putc_func)(int))
{
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
char* tgetstr(char* name, char** out)
{
#define CSI(x) "\x1b[" #x
#define SS3(x) "\x1bO" #x

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
    case 'cr': str = "\x0d"; break;
    case 'le': str = "\x08"; break;
    case 'nd': str = CSI(C); break;
    case 'up': str = CSI(A); break;

    // Cursor style
    case 've': str = CSI(?12l) CSI(?25h); break;
    case 'vs': str = CSI(?12;25h);        break;

    // Visible bell.
#ifdef CLINK_CHRISANT_MODS
    case 'vb': str = "\x1b_vb\x1b\\"; break;
#endif
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
