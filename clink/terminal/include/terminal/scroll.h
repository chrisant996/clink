// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
void set_scrolled_screen_buffer();
bool is_scroll_mode();
void reset_scroll_mode();
void init_deduce_scroll_mode();
void deduce_scroll_mode(HANDLE hout);

//------------------------------------------------------------------------------
enum SCRMODE
{
    SCR_BYLINE,
    SCR_BYPAGE,
    SCR_TOEND,
    SCR_ABSOLUTE,
};
int32 ScrollConsoleRelative(HANDLE h, int32 direction, SCRMODE mode);
