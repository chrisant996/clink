// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal.h"
#include "ecma48_terminal_out.h"
#include "win_screen_buffer.h"
#include "win_terminal_in.h"

#include <core/base.h>

//------------------------------------------------------------------------------
terminal terminal_create(screen_buffer* screen)
{
#if defined(PLATFORM_WINDOWS)
    if (screen == nullptr)
        screen = new win_screen_buffer(); // TODO: this leaks.

    return {
        new win_terminal_in(),
        new ecma48_terminal_out(*screen),
    };
#else
    return {};
#endif
}

//------------------------------------------------------------------------------
void terminal_destroy(const terminal& terminal)
{
    delete terminal.out;
    delete terminal.in;
}
