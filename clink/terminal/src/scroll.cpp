// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "scroll.h"

//------------------------------------------------------------------------------
// Terminal can't #include from Readline.
extern "C" int32 _rl_vis_botlin;
extern "C" int32 _rl_last_v_pos;

//------------------------------------------------------------------------------
static bool s_scroll_mode = false;

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
int32 ScrollConsoleRelative(HANDLE h, int32 direction, SCRMODE mode)
{
    // Get the current screen buffer window position.
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    // Calculate the bottom line of the readline edit line.
    SHORT bottom_Y = csbiInfo.dwCursorPosition.Y + (_rl_vis_botlin - _rl_last_v_pos);

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
