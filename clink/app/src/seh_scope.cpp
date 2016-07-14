// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "paths.h"
#include "seh_scope.h"

#include <core/str.h>

//------------------------------------------------------------------------------
static LONG WINAPI exception_filter(EXCEPTION_POINTERS* info)
{
#if defined(_MSC_VER)
    str<MAX_PATH> file_name;
    get_config_dir(file_name);
    file_name << "/mini_dump.dmp";

    fputs("\n!!! CLINK'S CRASHED!", stderr);
    fputs("\n!!! Something went wrong.", stderr);
    fputs("\n!!! Writing mini dump file to: ", stderr);
    fputs(file_name.c_str(), stderr);
    fputs("\n", stderr);

    HANDLE file = CreateFile(file_name.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD pid = GetCurrentProcessId();
        HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (process != nullptr)
        {
            MINIDUMP_EXCEPTION_INFORMATION mdei = { GetCurrentThreadId(), info, FALSE };
            MiniDumpWriteDump(process, pid, file, MiniDumpNormal, &mdei, nullptr, nullptr);
        }
        CloseHandle(process);
        CloseHandle(file);
    }
#endif // _MSC_VER

    // Would be awesome if we could unhook ourself, unload, and allow cmd.exe
    // to continue!

    return EXCEPTION_CONTINUE_SEARCH;
}



//------------------------------------------------------------------------------
seh_scope::seh_scope()
{
    m_prev_filter = SetUnhandledExceptionFilter(exception_filter);
}

//------------------------------------------------------------------------------
seh_scope::~seh_scope()
{
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)m_prev_filter);
}
