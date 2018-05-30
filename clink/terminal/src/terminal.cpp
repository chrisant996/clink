// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal.h"
#include "win_terminal_in.h"
#include "win_terminal_out.h"

#include <core/base.h>

//------------------------------------------------------------------------------
terminal terminal_create()
{
#if defined(PLATFORM_WINDOWS)
    return {
        new win_terminal_in(),
        new win_terminal_out(),
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
