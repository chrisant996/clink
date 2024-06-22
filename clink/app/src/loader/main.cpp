// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#define NOMINMAX
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN

#include <stdint.h>
#include <Windows.h>

//------------------------------------------------------------------------------
__declspec(dllimport) int32_t loader_main_thunk();

#if defined(_VC_NODEFAULTLIB)
#pragma runtime_checks("", off)
int32_t mainCRTStartup(uintptr_t param) // effectively a thread entry point.
#else
int32_t main(int32_t argc, char** argv)
#endif
{
    int32_t i = loader_main_thunk();
    ExitProcess(i); // without this Win10 will stall.
    return i;
}



//------------------------------------------------------------------------------
inline DWORD __strlen(const WCHAR* p)
{
    DWORD len = 0;
    while (p[len])
        len++;
    return len;
}

inline bool __streq(const WCHAR* a, const WCHAR* b)
{
    while (*a || *b)
        if (*(a++) != *(b++))
            return false;
    return true;
}

//------------------------------------------------------------------------------
// This is exported so that `clink testbed --hook` can simulate injection.
// This MUST import all APIs that Clink hooks, so that the EXE has IAT entries
// for them.
extern "C" {
__declspec(dllexport) void __stdcall testbed_hook_loop()
{
    const DWORD capacity = 4096;
    WCHAR* buffer = (WCHAR*)LocalAlloc(0, capacity * sizeof(*buffer));

    // Trigger initialization to finish.
    SetEnvironmentVariableW(L"clink_testbed", L"");

    // Satisfy importing SetEnvironmentStringsW.
    LPWCH strings = GetEnvironmentStringsW();
    if (strings)
    {
        SetEnvironmentStringsW(strings);
        FreeEnvironmentStringsW(strings);
    }

    DWORD num;
    while (true)
    {
        // Write prompt.
        GetEnvironmentVariableW(L"prompt", buffer, capacity);
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buffer, __strlen(buffer), &num, nullptr);

        // Read input.
        ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), (PVOID)buffer, capacity, &num, nullptr);

        // Gracefully exit.
        if (__streq(buffer, L"exit") ||
            __streq(buffer, L"exit\n") ||
            __streq(buffer, L"exit\r\n"))
            break;
    }
}
}