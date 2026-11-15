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

#ifdef _MSC_VER

bool g_use_gui_assertions = true;
void (*g_assertion_hook)() = nullptr;
static volatile long s_num_assertions = 0;

long get_num_assertions()
{
    return s_num_assertions;
}

#endif // _MSC_VER

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

#ifdef _MSC_VER

extern "C" void _wassert(wchar_t const* message, wchar_t const* file, unsigned line)
{
    const auto num = InterlockedIncrement(&test::s_num_assertions);

    wchar_t tmp[32];
    _itow_s(line, tmp, 10);

    if (!test::g_use_gui_assertions)
        printf_s("<<<<assertion #%u\r\n", num); // Effectively two newlines.

    wchar_t wbuffer[4096];

    if (test::g_use_gui_assertions)
    {
        wcscpy_s(wbuffer, L"ASSERT:  ");
        wcscat_s(wbuffer, file);
        wcscat_s(wbuffer, L" @li");
        wcscat_s(wbuffer, tmp);
        wcscat_s(wbuffer, L":  ");
        wcscat_s(wbuffer, message);
        wcscat_s(wbuffer, L"\r\n");
        OutputDebugStringW(wbuffer);
    }

    wbuffer[0] = 0;
    if (!test::g_use_gui_assertions)
        wcscat_s(wbuffer, L"ASSERT:  ");
    wcscat_s(wbuffer, _countof(wbuffer), message);
    if (test::g_use_gui_assertions)
        wcscat_s(wbuffer, L"\r\n");
    wcscat_s(wbuffer, L"\r\n\r\nFile: ");
    wcscat_s(wbuffer, file);
    wcscat_s(wbuffer, L"\r\nLine: ");
    wcscat_s(wbuffer, tmp);

    int outcome = IDOK;
    if (test::g_use_gui_assertions)
    {
        outcome = MessageBoxW(nullptr, wbuffer, L"ASSERT", MB_ICONEXCLAMATION|MB_ABORTRETRYIGNORE|MB_DEFBUTTON3);
        if (outcome == IDIGNORE && GetKeyState(VK_CONTROL) >= 0)
            outcome = IDOK;
    }
    else
    {
        _putws(wbuffer);
        if (num >= 99)
        {
            printf_s("ABORTING EXECUTION; TOO MANY ASSERTIONS (%u)!\r\n", num);
            outcome = IDABORT;
        }
        fflush(stdout);
        if (test::g_assertion_hook)
            test::g_assertion_hook();
    }

    switch (outcome)
    {
    case IDABORT:   TerminateProcess(GetCurrentProcess(), -1); break;
    case IDRETRY:   DebugBreak(); break;
    case IDIGNORE:  break;
    }
}

#endif // _MSC_VER
