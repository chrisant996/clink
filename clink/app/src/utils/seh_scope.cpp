// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "seh_scope.h"
#include "utils/app_context.h"
#include "version.h"

#include <core/str.h>

//------------------------------------------------------------------------------
static thread_local int32 s_filter = 0;

//------------------------------------------------------------------------------
static LONG WINAPI exception_filter(EXCEPTION_POINTERS* info)
{
    if (s_filter <= 0)
        return EXCEPTION_CONTINUE_SEARCH;

#if defined(_MSC_VER)
    str<MAX_PATH, false> buffer;
    if (const app_context* context = app_context::get())
        context->get_state_dir(buffer);
    else
    {
        app_context::desc desc;
        app_context app_context(desc);
        app_context.get_state_dir(buffer);
    }
    buffer << "\\clink.dmp";

    wstr<> wpath(buffer.c_str());

    const DWORD pid = GetCurrentProcessId();
    fputs("\n!!! CLINK'S CRASHED!", stderr);
#ifdef _WIN64
    fputs("\n!!! v" CLINK_VERSION_STR " (x64)", stderr);
#else
    fputs("\n!!! v" CLINK_VERSION_STR " (x86)", stderr);
#endif
    fprintf(stderr, "\n!!! process id %u (0x%x)", pid, pid);
    fputs("\n!!!", stderr);
    fputs("\n!!! Writing core dump", stderr);
    fputs("\n!!! ", stderr);

    DWORD dummy;
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (GetConsoleMode(h, &dummy))
    {
        DWORD written;
        WriteConsoleW(h, wpath.c_str(), wpath.length(), &written, nullptr);
    }
    else
    {
        fputs(buffer.c_str(), stderr);
    }

    // Write a core dump file.
    BOOL ok = FALSE;
    HANDLE file = CreateFileW(wpath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD pid = GetCurrentProcessId();
        HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (process != nullptr)
        {
            MINIDUMP_EXCEPTION_INFORMATION mdei = { GetCurrentThreadId(), info, FALSE };
            ok = MiniDumpWriteDump(process, pid, file, MiniDumpNormal, &mdei, nullptr, nullptr);
        }
        CloseHandle(process);
        CloseHandle(file);
    }
    fputs(ok ? "\n!!! ...ok" : "\n!!! ...failed", stderr);
    fputs("\n!!!", stderr);

    // Output some useful modules' base addresses
    buffer.format("\n!!! Clink: 0x%p", GetModuleHandle(CLINK_DLL));
    fputs(buffer.c_str(), stderr);

    buffer.format("\n!!! Host: 0x%p", GetModuleHandle(nullptr));
    fputs(buffer.c_str(), stderr);

    // Output information about the exception that occurred.
    EXCEPTION_RECORD& record = *(info->ExceptionRecord);
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        uintptr_t access = record.ExceptionInformation[0];
        uintptr_t addr = record.ExceptionInformation[1];
        buffer.format("\n!!! Guru: 0x%08x addr=0x%p (%d)", record.ExceptionCode, addr, access);
    }
    else
        buffer.format("\n!!! Guru: 0x%08x", record.ExceptionCode);
    fputs(buffer.c_str(), stderr);

    // Output a backtrace.
    fputs("\n!!!", stderr);
    fputs("\n!!! Backtrace:", stderr);
    bool skip = true;
    void* backtrace[48];
    int32 bt_length = CaptureStackBackTrace(0, sizeof_array(backtrace), backtrace, nullptr);
    for (int32 i = 0; i < bt_length; ++i)
    {
        if (skip &= (backtrace[i] != record.ExceptionAddress))
            continue;

        buffer.format("\n!!! 0x%p", backtrace[i]);
        fputs(buffer.c_str(), stderr);
    }

    fputs("\n\nPress Enter to exit...", stderr);
    fgetc(stdin);
#endif // _MSC_VER

    return EXCEPTION_CONTINUE_SEARCH;
}



//------------------------------------------------------------------------------
seh_scope::seh_scope()
{
    ++s_filter;
}

//------------------------------------------------------------------------------
seh_scope::~seh_scope()
{
    --s_filter;
}

//------------------------------------------------------------------------------
void install_exception_filter()
{
    SetUnhandledExceptionFilter(exception_filter);
}
