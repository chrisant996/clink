// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <windows.h>

// For compatibility with Windows 8.1 SDK.
#if !defined( ENABLE_VIRTUAL_TERMINAL_PROCESSING )
# define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x0004
#elif ENABLE_VIRTUAL_TERMINAL_PROCESSING != 0x0004
# error ENABLE_VIRTUAL_TERMINAL_PROCESSING must be 0x0004
#endif

namespace clatch {

//------------------------------------------------------------------------------
void colors::initialize()
{
#pragma warning(push)
#pragma warning(disable:4996)
    DWORD type;
    DWORD data;
    DWORD size;
    LSTATUS status = RegGetValue(HKEY_CURRENT_USER, "Console", "ForceV2", RRF_RT_REG_DWORD, &type, &data, &size);
    if (status != ERROR_SUCCESS ||
        type != REG_DWORD ||
        size != sizeof(data) ||
        data != 0)
    {
        DWORD mode;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (GetConsoleMode(h, &mode))
        {
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            *get_colored_storage() = true;
        }
    }
#pragma warning(pop)
}

};
