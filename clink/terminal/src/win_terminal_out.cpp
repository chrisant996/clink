// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_terminal_out.h"

#include <core/base.h>
#include <core/str_iter.h>

//------------------------------------------------------------------------------
enum
{
    attr_mask_fg        = 0x000f,
    attr_mask_bg        = 0x00f0,
    attr_mask_bold      = 0x0008,
    attr_mask_underline = 0x8000,
    attr_mask_all       = attr_mask_fg|attr_mask_bg|attr_mask_underline,
};



//------------------------------------------------------------------------------
void win_terminal_out::begin()
{
    m_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(m_stdout, &m_prev_mode);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    m_default_attr = csbi.wAttributes & attr_mask_all;
    m_bold = !!(m_default_attr & attr_mask_bold);
}

//------------------------------------------------------------------------------
void win_terminal_out::end()
{
    SetConsoleTextAttribute(m_stdout, m_default_attr);
    SetConsoleMode(m_stdout, m_prev_mode);
    m_stdout = nullptr;
}

//------------------------------------------------------------------------------
void win_terminal_out::write(const char* chars, int length)
{
    str_iter iter(chars, length);
    while (length > 0)
    {
        wchar_t wbuf[384];
        int n = min<int>(sizeof_array(wbuf), length + 1);
        n = to_utf16(wbuf, n, iter);

        write(wbuf, n);

        n = int(iter.get_pointer() - chars);
        length -= n;
        chars += n;
    }
}

//------------------------------------------------------------------------------
void win_terminal_out::write(const wchar_t* chars, int length)
{
    DWORD written;
    WriteConsoleW(m_stdout, chars, length, &written, nullptr);
}

//------------------------------------------------------------------------------
void win_terminal_out::flush()
{
    // When writing to the console conhost.exe will restart the cursor blink
    // timer and hide it which can be disorientating, especially when moving
    // around a line. The below will make sure it stays visible.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    SetConsoleCursorPosition(m_stdout, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
int win_terminal_out::get_columns() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    return csbi.dwSize.X;
}

//------------------------------------------------------------------------------
int win_terminal_out::get_rows() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    return (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
}

//------------------------------------------------------------------------------
void win_terminal_out::set_attributes(const attributes attr)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);

    int out_attr = csbi.wAttributes & attr_mask_all;

    // Note to self; lookup table is probably much faster.
    auto swizzle = [] (int rgbi) {
        int b_r_ = ((rgbi & 0x01) << 2) | !!(rgbi & 0x04);
        return (rgbi & 0x0a) | b_r_;
    };

    // Bold
    if (auto bold_attr = attr.get_bold())
        m_bold = !!(bold_attr.value);

    // Underline
    if (auto underline = attr.get_underline())
    {
        if (underline.value)
            out_attr |= attr_mask_underline;
        else
            out_attr &= ~attr_mask_underline;
    }

    // Foreground colour
    bool bold = m_bold;
    if (auto fg = attr.get_fg())
    {
        int value = fg.is_default ? m_default_attr : swizzle(fg.value.value);
        value &= attr_mask_fg;
        out_attr = (out_attr & attr_mask_bg) | value;
        bold |= (value > 7);
    }
    else
        bold |= (out_attr & attr_mask_bold) != 0;

    if (bold)
        out_attr |= attr_mask_bold;
    else
        out_attr &= ~attr_mask_bold;

    // Background colour
    if (auto bg = attr.get_bg())
    {
        int value = bg.is_default ? m_default_attr : (swizzle(bg.value.value) << 4);
        out_attr = (out_attr & attr_mask_fg) | (value & attr_mask_bg);
    }

    // TODO: add rgb/xterm256 support back.

    out_attr |= csbi.wAttributes & ~attr_mask_all;
    SetConsoleTextAttribute(m_stdout, short(out_attr));
}
