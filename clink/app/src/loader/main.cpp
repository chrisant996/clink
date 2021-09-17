// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <stdint.h>
#include <Windows.h>

//------------------------------------------------------------------------------
__declspec(dllimport) int loader_main_thunk();

#if defined(_VC_NODEFAULTLIB)
#pragma runtime_checks("", off)
int mainCRTStartup(uintptr_t param) // effectively a thread entry point.
#else
int main(int argc, char** argv)
#endif
{
    int i = loader_main_thunk();
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
extern "C" {
__declspec(dllexport) void __stdcall testbed_hook_loop()
{
    const DWORD capacity = 4096;
    WCHAR* buffer = (WCHAR*)LocalAlloc(0, capacity * sizeof(*buffer));

    // Trigger initialization to finish.
    SetEnvironmentVariableW(L"clink_testbed", L"");

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