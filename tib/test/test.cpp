// Copyright (c) 2021,2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"

#ifdef _WIN32
// For compatibility with Windows 8.1 SDK.
#if !defined( ENABLE_VIRTUAL_TERMINAL_PROCESSING )
# define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x0004
#elif ENABLE_VIRTUAL_TERMINAL_PROCESSING != 0x0004
# error ENABLE_VIRTUAL_TERMINAL_PROCESSING must be 0x0004
#endif
#endif

namespace test {

void colors::initialize()
{
#ifdef _WIN32
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif
    DWORD type;
    DWORD data;
    DWORD size;
    LSTATUS status = RegGetValueA(HKEY_CURRENT_USER, "Console", "ForceV2", RRF_RT_REG_DWORD, &type, &data, &size);
    if (status != ERROR_SUCCESS ||
        type != REG_DWORD ||
        size != sizeof(data) ||
        data != 0)
    {
        DWORD mode;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (GetConsoleMode(h, &mode))
        {
#if 0
            // REDUNDANT:  auto_terminal_init in signaled.cpp sets the mode.
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
            *get_colored_storage() = true;
        }
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
}

} // namespace test
