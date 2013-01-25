/* Copyright (c) 2012 Martin Ridgers
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
#include "shared/util.h"

//------------------------------------------------------------------------------
int                         _rl_dispatch(int, Keymap);
extern int                  rl_key_sequence_length;
static COORD                g_cursor_position;
static KEYMAP_ENTRY_ARRAY   g_scroller_keymap;
static Keymap               g_previous_keymap;

//------------------------------------------------------------------------------
static int leave_scroll_mode(int count, int invoking_key)
{
    HANDLE handle;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleCursorPosition(handle, g_cursor_position);

    // Dispatch key to previous keymap, but only if it's a single key. This is
    // so users don't get disorientate when leaving scroll mode via arrow keys.
    rl_set_keymap(g_previous_keymap);
    if (invoking_key && rl_key_sequence_length == 1)
    {
        _rl_dispatch(invoking_key, g_previous_keymap);
    }

    return 0;
}

//------------------------------------------------------------------------------
static int page_up(int count, int invoking_key)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE handle;
    int rows_per_page;
    SMALL_RECT* wnd;
    
    if (rl_key_sequence_length < 3)
    {
        return leave_scroll_mode(count, invoking_key);
    }

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);
    wnd = &csbi.srWindow;

    rows_per_page = wnd->Bottom - wnd->Top - 1;
    if (rows_per_page > wnd->Top)
    {
        rows_per_page = wnd->Top;
    }

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Top - rows_per_page;
    SetConsoleCursorPosition(handle, csbi.dwCursorPosition);

    return 0;
}

//------------------------------------------------------------------------------
static int page_down(int count, int invoking_key)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE handle;
    int rows_per_page;
    SMALL_RECT* wnd;
    
    if (rl_key_sequence_length < 3)
    {
        return leave_scroll_mode(count, invoking_key);
    }

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);
    wnd = &csbi.srWindow;

    rows_per_page = wnd->Bottom - wnd->Top - 1;

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Bottom + rows_per_page;
    if (csbi.dwCursorPosition.Y > g_cursor_position.Y)
    {
        csbi.dwCursorPosition.Y = g_cursor_position.Y;
    }

    SetConsoleCursorPosition(handle, csbi.dwCursorPosition);

    return 0;
}

//------------------------------------------------------------------------------
void enter_scroll_mode(int scroll_one_page)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE handle;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);

    g_cursor_position = csbi.dwCursorPosition;

    switch (scroll_one_page)
    {
    case -1:    page_up(0, 0);    break;
    case 1:     page_down(0, 0);  break;
    }

    g_previous_keymap = rl_get_keymap();
    rl_set_keymap(g_scroller_keymap);
}

//------------------------------------------------------------------------------
void initialise_rl_scroller()
{
    int i;

    for (i = 0; i < sizeof_array(g_scroller_keymap); ++i)
    {
        rl_bind_key_in_map(i, leave_scroll_mode, g_scroller_keymap);
    }

    rl_generic_bind(ISKMAP, "\033", (char*)g_scroller_keymap, g_scroller_keymap);
    rl_generic_bind(ISKMAP, "`", (char*)g_scroller_keymap, g_scroller_keymap);
    rl_bind_key_in_map('c', page_up, g_scroller_keymap);
    rl_bind_key_in_map('h', page_down, g_scroller_keymap);
}

// vim: expandtab
