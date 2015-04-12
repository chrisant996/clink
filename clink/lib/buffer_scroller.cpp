/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "buffer_scroller.h"

//------------------------------------------------------------------------------
buffer_scroller::buffer_scroller()
: m_handle(0)
{
    m_cursor_position.X = 0;
    m_cursor_position.Y = 0;
}

//------------------------------------------------------------------------------
void buffer_scroller::begin()
{
    m_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);

    m_cursor_position = csbi.dwCursorPosition;
}

//------------------------------------------------------------------------------
void buffer_scroller::end()
{
    SetConsoleCursorPosition(m_handle, m_cursor_position);
    m_handle = 0;
    m_cursor_position.X = 0;
    m_cursor_position.Y = 0;
}

//------------------------------------------------------------------------------
void buffer_scroller::page_up()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SMALL_RECT* wnd = &csbi.srWindow;

    int rows_per_page = wnd->Bottom - wnd->Top - 1;
    if (rows_per_page > wnd->Top)
        rows_per_page = wnd->Top;

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Top - rows_per_page;
    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
void buffer_scroller::page_down()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SMALL_RECT* wnd = &csbi.srWindow;

    int rows_per_page = wnd->Bottom - wnd->Top - 1;

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Bottom + rows_per_page;
    if (csbi.dwCursorPosition.Y > m_cursor_position.Y)
        csbi.dwCursorPosition.Y = m_cursor_position.Y;

    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}
