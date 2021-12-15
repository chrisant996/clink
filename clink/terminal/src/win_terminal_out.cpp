// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_terminal_out.h"
#include "find_line.h"

#include <core/base.h>
#include <core/str_iter.h>

//------------------------------------------------------------------------------
win_terminal_out::~win_terminal_out()
{
    free(m_attrs);
    free(m_chars);
}

//------------------------------------------------------------------------------
void win_terminal_out::begin()
{
    assert(!m_stdout);

    m_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(m_stdout, &m_prev_mode);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    m_default_attr = csbi.wAttributes;
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

        DWORD written;
        WriteConsoleW(m_stdout, wbuf, n, &written, nullptr);

        n = int(iter.get_pointer() - chars);
        length -= n;
        chars += n;
    }
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
bool win_terminal_out::get_line_text(int line, str_base& out) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_stdout, &csbi))
        return false;

    if (!ensure_chars_buffer(csbi.dwSize.X))
        return false;

    COORD coord = { 0, SHORT(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputCharacterW(m_stdout, m_chars, csbi.dwSize.X, coord, &len))
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
int win_terminal_out::is_line_default_color(int line) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_stdout, &csbi))
        return -1;

    if (!ensure_attrs_buffer(csbi.dwSize.X))
        return -1;

    COORD coord = { 0, SHORT(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputAttribute(m_stdout, m_attrs, csbi.dwSize.X, coord, &len))
        return -1;
    if (len != csbi.dwSize.X)
        return -1;

    for (const WORD* attr = m_attrs; len--; attr++)
        if (*attr != m_default_attr)
            return false;

    return true;
}

//------------------------------------------------------------------------------
int win_terminal_out::line_has_color(int line, const BYTE* attrs, int num_attrs, BYTE mask) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_stdout, &csbi))
        return -1;

    if (!ensure_attrs_buffer(csbi.dwSize.X))
        return -1;

    COORD coord = { 0, SHORT(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputAttribute(m_stdout, m_attrs, csbi.dwSize.X, coord, &len))
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
int win_terminal_out::find_line(int starting_line, int distance, const char* text, find_line_mode mode, const BYTE* attrs, int num_attrs, BYTE mask) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_stdout, &csbi))
        return -2;

    if (text && !ensure_chars_buffer(csbi.dwSize.X))
        return -2;
    if (attrs && num_attrs > 0 && !ensure_attrs_buffer(csbi.dwSize.X))
        return -2;

    return ::find_line(m_stdout, csbi,
                       m_chars, m_chars_capacity,
                       m_attrs, m_attrs_capacity,
                       starting_line, distance,
                       text, mode,
                       attrs, num_attrs, mask);
}

//------------------------------------------------------------------------------
bool win_terminal_out::ensure_chars_buffer(int width) const
{
    if (width > m_chars_capacity)
    {
        m_chars = static_cast<WCHAR*>(malloc((width + 1) * sizeof(*m_chars)));
        if (!m_chars)
            return false;
        m_chars_capacity = width;
    }
    return true;
}

//------------------------------------------------------------------------------
bool win_terminal_out::ensure_attrs_buffer(int width) const
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
