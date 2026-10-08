// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal.h"
#include "ecma48_terminal_out.h"
#include "win_screen_buffer.h"
#include "win_terminal_in.h"

#include <core/base.h>
#include <core/os.h>

//------------------------------------------------------------------------------
static bool s_has_synchronize_output = false;

//------------------------------------------------------------------------------
void terminal_discover_config(terminal_in* in)
{
    str<> response;

    // Reset config states.

    s_has_synchronize_output = false;

    // Send terminal queries.

    if (os::get_env("CLINK_SYNCHRONIZE_OUTPUT", response))
    {
        s_has_synchronize_output = (atoi(response.c_str()) > 0);
    }
    else if (in->send_terminal_request("\x1b[?2026$p", "\x1b[?2026;", "y", response))
    {
        s_has_synchronize_output = (strstr(response.c_str(), ";1$y") ||
                                    strstr(response.c_str(), ";2$y"));
    }
}

//------------------------------------------------------------------------------
bool terminal_has_synchronize_output()
{
    return s_has_synchronize_output;
}
