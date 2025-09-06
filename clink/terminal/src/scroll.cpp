// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "scroll.h"
#include "screen_buffer.h"  // for get_native_ansi_handler().

//------------------------------------------------------------------------------
// Terminal can't #include from Lib.
extern SHORT calc_max_y_scroll_pos(SHORT y);

//------------------------------------------------------------------------------
static bool s_scroll_mode = false;
static SHORT s_top_for_deduce = 0;

//------------------------------------------------------------------------------
bool is_scroll_mode()
{
    return s_scroll_mode;
}

//------------------------------------------------------------------------------
void reset_scroll_mode()
{
    s_scroll_mode = false;
}

//------------------------------------------------------------------------------
void init_deduce_scroll_mode()
{
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hout, &csbi))
        s_top_for_deduce = csbi.srWindow.Top;
    else
        s_top_for_deduce = 0;
}

//------------------------------------------------------------------------------
void deduce_scroll_mode(HANDLE hout)
{
    if (!s_scroll_mode)
    {
        switch (get_native_ansi_handler())
        {
        case ansi_handler::winterminal:
        case ansi_handler::wezterm:
            break;
        default:
            {
                // Try to deduce whether the console has been scrolled by the
                // scrollbar.  Otherwise using the scrollbar to scroll the
                // terminal followed by a mouse click on the window will reset
                // the scroll position.
                CONSOLE_SCREEN_BUFFER_INFO csbiAfter;
                if (GetConsoleScreenBufferInfo(hout, &csbiAfter))
                {
                    // If srWindow has moved UP while waiting for input, then
                    // it's reasonable safe to assume it was due to some
                    // user-induced scroll action.  But if srWindow moved DOWN
                    // while waiting for input, that might not be from user
                    // action:  it could also be from output that went to the
                    // console, and it's better not to interfere with that.
                    if (csbiAfter.srWindow.Top < s_top_for_deduce)
                        s_scroll_mode = true;
                }
            }
            break;
        }
    }
}

//------------------------------------------------------------------------------
int32 ScrollConsoleRelative(HANDLE h, int32 direction, SCRMODE mode)
{
    // Get the current screen buffer window position.
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    // Calculate the bottom line of the readline edit line.
    SHORT bottom_Y = calc_max_y_scroll_pos(csbiInfo.dwCursorPosition.Y);

    // Calculate the new window position.
    SMALL_RECT srWindow = csbiInfo.srWindow;
    SHORT cy = srWindow.Bottom - srWindow.Top;
    if (mode == SCR_ABSOLUTE)
    {
        if (direction <= 1)
            direction = 0;

        srWindow.Top = direction;
        srWindow.Bottom = srWindow.Top + cy;
    }
    else if (mode == SCR_TOEND)
    {
        if (direction <= 0)
            srWindow.Top = srWindow.Bottom = -1;
        else
            srWindow.Bottom = csbiInfo.dwSize.Y;
    }
    else
    {
        if (mode == SCR_BYPAGE)
            direction *= csbiInfo.srWindow.Bottom - csbiInfo.srWindow.Top;
        srWindow.Top += (SHORT)direction;
        srWindow.Bottom += (SHORT)direction;
    }

    // Check whether the window is too close to the screen buffer top.
    if (srWindow.Bottom >= bottom_Y)
    {
        srWindow.Bottom = bottom_Y;
        srWindow.Top = srWindow.Bottom - cy;
    }
    if (srWindow.Top < 0)
    {
        srWindow.Top = 0;
        srWindow.Bottom = srWindow.Top + cy;
    }

    // Set the new window position.
    s_scroll_mode = true;
    if (srWindow.Top == csbiInfo.srWindow.Top ||
        !SetConsoleWindowInfo(h, TRUE/*fAbsolute*/,  &srWindow))
        return 0;

    // Tell printer so it can work around a problem with WriteConsoleW.
    set_scrolled_screen_buffer();

    return srWindow.Top - csbiInfo.srWindow.Top;
}
