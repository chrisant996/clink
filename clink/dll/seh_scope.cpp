/* Copyright (c) 2013 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "seh_scope.h"
#include "shared/util.h"

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
