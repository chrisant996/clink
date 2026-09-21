// Copyright (c) 2026 by Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_host.h"

static bool s_signaled = false;

namespace tib_host {

bool is_signaled()
{
    return s_signaled;
}

void clear_signaled()
{
    s_signaled = false;
}

auto_terminal_init::~auto_terminal_init()
{
    restore();
}

auto_terminal_init::auto_terminal_init()
{
#ifdef _WIN32
    HANDLE handles[3] = {};
    for (int i = 0; i < 3; ++i)
    {
        handles[i] = GetStdHandle(STD_INPUT_HANDLE - i);
        if (handles[i] && GetConsoleMode(handles[i], &m_orig_modes[i]))
            m_restore_modes |= uint8_t(1 << i);
    }

    SetConsoleCtrlHandler(BreakHandler, true);

    if (m_restore_modes & 0x01)
        SetConsoleMode(handles[0], m_orig_modes[0]&~(ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_WINDOW_INPUT));
    if (m_restore_modes & 0x02)
        SetConsoleMode(handles[1], m_orig_modes[1]|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    if (m_restore_modes & 0x04)
        SetConsoleMode(handles[2], m_orig_modes[2]|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    // TODO-LINUX: POSIX sigaction alternative.
#endif
}

void auto_terminal_init::restore()
{
#ifdef _WIN32
    const uint8_t restore_modes = m_restore_modes;
    if (restore_modes)
    {
        m_restore_modes = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (restore_modes & (1 << i))
            {
                HANDLE h = GetStdHandle(STD_INPUT_HANDLE - i);
                if (h)
                    SetConsoleMode(h, m_orig_modes[i]);
            }
        }
    }
#else
        // TODO-LINUX: POSIX sigaction alternative.
#endif
}

#ifdef _WIN32
BOOL auto_terminal_init::BreakHandler(DWORD CtrlType)
{
    if (CtrlType == CTRL_C_EVENT || CtrlType == CTRL_BREAK_EVENT)
    {
        // Do not terminate on Ctrl-C or Ctrl-Break.
        s_signaled = true;
        return true;
    }
    return false;
}
#else
    // TODO-LINUX: POSIX sigaction alternative.
#endif

} // namespace tib_host
