// Copyright (c) 2015 Martin Ridgers
// Portions Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clink_ctrlevent.h"

#include <signal.h>
#include <assert.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
}

//------------------------------------------------------------------------------
static bool clink_rl_cleanup_needed = false;
bool g_suppress_signal_assert = false;
void (*after_signal_hook_fn)() = nullptr;

//------------------------------------------------------------------------------
static void clink_reset_event_hook()
{
    rl_signal_event_hook = nullptr;
    clink_set_signaled(0);
    clink_rl_cleanup_needed = false;
}

//------------------------------------------------------------------------------
static int32 clink_event_hook()
{
    if (clink_rl_cleanup_needed)
    {
        rl_callback_sigcleanup();
        rl_echo_signal_char(SIGBREAK);
    }

    _rl_move_vert(_rl_vis_botlin);
    rl_crlf();
    _rl_last_c_pos = 0;

    if (after_signal_hook_fn)
        after_signal_hook_fn();
    clink_reset_event_hook();
    return 0;
}

//------------------------------------------------------------------------------
static void clink_set_event_hook()
{
    rl_signal_event_hook = clink_event_hook;
}

//------------------------------------------------------------------------------
void clink_sighandler(int32 sig)
{
    // raise() clears the signal handler, so set it again.
    signal(sig, clink_sighandler);
    clink_set_event_hook();
    clink_set_signaled(sig);
    clink_rl_cleanup_needed = !RL_ISSTATE(RL_STATE_SIGHANDLER);
}

//------------------------------------------------------------------------------
bool clink_maybe_handle_signal()
{
    const bool signaled = clink_is_signaled();
    if (signaled)
    {
        if (rl_signal_event_hook)
            (*rl_signal_event_hook)();
        else
            clink_reset_event_hook();
    }
    return signaled;
}
