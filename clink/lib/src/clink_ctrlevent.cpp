// Copyright (c) 2015 Martin Ridgers
// Portions Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <signal.h>
#include <assert.h>

//------------------------------------------------------------------------------
extern void interrupt_input();

//------------------------------------------------------------------------------
static volatile int32 clink_signal = 0;
static int32 s_ctrlevent_install_count = 0;

//------------------------------------------------------------------------------
int32 clink_is_signaled()
{
    return clink_signal;
}

//------------------------------------------------------------------------------
void clink_set_signaled(int32 sig)
{
    clink_signal = sig;
}

//------------------------------------------------------------------------------
static BOOL WINAPI clink_ctrlevent_handler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT)
    {
        clink_signal = (ctrl_type == CTRL_C_EVENT) ? SIGINT : SIGBREAK;
        interrupt_input();
    }
    else if (ctrl_type == CTRL_CLOSE_EVENT)
    {
        // Issue 296 reported that on some computers the addition of signal
        // handling caused the OS to show a dialog box because CMD takes too
        // long to exit.  I have no idea how the signal handler could cause
        // that; docs for SetConsoleCtrlHandler state that regardless what the
        // handler returns for CTRL_CLOSE_EVENT, the OS will terminate the
        // process.  But on at least one computer that is not proving to be
        // true for some unknown reason.  I think there has to be external
        // software involved somehow.  The issue was reported on a work computer
        // that has antivirus and other software installed by policy, so it is a
        // plausible hypothesis.
        //
        // Surprisingly, the C runtime treats CTRL_CLOSE_EVENT the same as
        // CTRL_BREAK_EVENT.  So, there is effectively a signal handler
        // installed for CTRL_CLOSE_EVENT, even though there is no intent to
        // do anything in response to CTRL_CLOSE_EVENT, and it ends up returning
        // TRUE instead of FALSE.  That shouldn't have any effect, but it's the
        // only observable difference within Clink, so maybe it's somehow
        // interfering with some other software that participates in system
        // operations.  Since the CTRL_CLOSE_EVENT should result in guaranteed
        // termination, there's no need to preserve installed signal handlers.
        // So, by removing the SIGBREAK handler, it should remove the TRUE vs
        // FALSE difference and restore the previous behavior.
        signal(SIGBREAK, nullptr);
    }
    return false;
}

//------------------------------------------------------------------------------
void clink_install_ctrlevent()
{
    assert(s_ctrlevent_install_count == 0);
    s_ctrlevent_install_count++;

    SetConsoleCtrlHandler(clink_ctrlevent_handler, true);
}

//------------------------------------------------------------------------------
void clink_shutdown_ctrlevent()
{
    if (s_ctrlevent_install_count)
    {
        assert(s_ctrlevent_install_count == 1);
        s_ctrlevent_install_count--;

        SetConsoleCtrlHandler(clink_ctrlevent_handler, false);
    }
}
