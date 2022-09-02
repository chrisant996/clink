// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal.h"
#include "ecma48_terminal_out.h"
#include "win_screen_buffer.h"
#include "win_terminal_in.h"

#include <core/base.h>

//------------------------------------------------------------------------------
terminal terminal_create(screen_buffer* screen, bool cursor_visibility)
{
#if defined(PLATFORM_WINDOWS)
    terminal term;
    term.screen_owned = (screen == nullptr);
    term.screen = screen ? screen : new win_screen_buffer();
    term.in = new win_terminal_in(cursor_visibility);
    term.out = new ecma48_terminal_out(*term.screen);
    return term;
#else
    return {};
#endif
}

//------------------------------------------------------------------------------
void terminal_destroy(const terminal& terminal)
{
    delete terminal.out;
    delete terminal.in;
    if (terminal.screen_owned)
        delete terminal.screen;
}
