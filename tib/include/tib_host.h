// Copyright (c) 2026 by Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

namespace tib_host {

#ifdef _WIN32
void set_crt_locale_utf8();
void set_console_vt_input();
#endif

bool is_signaled();
void clear_signaled();

class auto_terminal_init
{
public:
                        ~auto_terminal_init();
                        auto_terminal_init();

private:
    void                restore();

#ifdef _WIN32
    static BOOL WINAPI  BreakHandler(DWORD CtrlType);
#endif

private:
#ifdef _WIN32
    DWORD               m_orig_modes[3];
    uint8_t             m_restore_modes = 0;
#else
    // TODO-LINUX: POSIX sigaction alternative.
#endif
};

} // namespace tib_host
