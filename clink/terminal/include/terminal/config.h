// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
// Scoped configuration of console mode.
//
// Clear 'processed input' flag so key presses such as Ctrl-C and Ctrl-S aren't
// swallowed.  We also want events about window size changes.
class console_config
{
public:
    console_config(HANDLE handle=NULL) : m_handle(handle ? handle : GetStdHandle(STD_INPUT_HANDLE))
    {
        extern void save_host_input_mode(DWORD);
        GetConsoleMode(m_handle, &m_prev_mode);
        save_host_input_mode(m_prev_mode);
        SetConsoleMode(m_handle, ENABLE_WINDOW_INPUT);
    }

    ~console_config()
    {
        SetConsoleMode(m_handle, m_prev_mode);
    }

private:
    const HANDLE    m_handle;
    DWORD           m_prev_mode;
};
