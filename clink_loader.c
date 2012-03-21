/* Copyright (c) 2012 Martin Ridgers
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

#include <Windows.h>
#include <Tlhelp32.h>

#include <stdio.h>

#include "clink.h"

//------------------------------------------------------------------------------
static DWORD get_parent_pid()
{
    ULONG_PTR pbi[6];
    ULONG size = 0;
    LONG (WINAPI *NtQueryInformationProcess)(
        HANDLE,
        ULONG,
        PVOID,
        ULONG,
        PULONG
    );

    *(FARPROC*)&NtQueryInformationProcess = GetProcAddress(
        LoadLibraryA("ntdll.dll"),
        "NtQueryInformationProcess"
    );

    if (NtQueryInformationProcess)
    {
        LONG ret = NtQueryInformationProcess(
            GetCurrentProcess(),
            0,
            &pbi,
            sizeof(pbi),
            &size
        );

        if ((ret >= 0) && (size == sizeof(pbi)))
        {
            return (DWORD)(pbi[5]);
        }
    }

    return -1;
}

//------------------------------------------------------------------------------
static void toggle_threads(DWORD pid, int on)
{
    BOOL ok;
    THREADENTRY32 te;

    HANDLE th32 = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid);

    ok = Thread32First(th32, &te);
    while (ok != FALSE)
    {
        HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (on)
        {
            ResumeThread(thread);
        }
        else
        {
            SuspendThread(thread);
        }
        CloseHandle(thread);
        Thread32Next(th32, &te);
    }

    CloseHandle(th32);
}

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    int ret = 0;
    DWORD parent_pid;
    HANDLE parent_process;
    BOOL is_wow_64[2];
    DWORD thread_id;
    LPVOID buffer;
    BOOL t;
    void* thread_proc;
    char dll_path[512];
    unsigned dll_path_size = sizeof(dll_path);
    HANDLE remote_thread;

#ifdef __MINGW32__
    typedef BOOL (WINAPI *_IsWow64Process)(HANDLE, BOOL*);
    _IsWow64Process IsWow64Process = (_IsWow64Process)GetProcAddress(
        LoadLibraryA("kernel32.dll"),
        "IsWow64Process"
    );
#endif // __MINGW32__

    GetCurrentDirectory(dll_path_size, dll_path);
    strcat(dll_path, "\\");
    strcat(dll_path, CLINK_DLL_NAME);

    parent_pid = get_parent_pid();
    if (parent_pid == -1)
    {
        fputs("Failed to find parent PID.\n", stderr);
        return -1;
    }

#ifdef CLINK_DEBUG
    printf("Parent PID: %d\n", parent_pid);
#endif

    parent_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, parent_pid);
    if (parent_process == NULL)
    {
        fputs("Failed to open parent process.\n", stderr);
        return -1;
    }

    IsWow64Process(parent_process, is_wow_64);
    IsWow64Process(GetCurrentProcess(), is_wow_64 + 1);
    if (is_wow_64[0] != is_wow_64[1])
    {
        fputs("32/64-bit mismatch.\n", stderr);
        return -1;
    }

    buffer = VirtualAllocEx(
        parent_process,
        NULL,
        dll_path_size,
        MEM_COMMIT,
        PAGE_READWRITE
    );
    if (buffer == NULL)
    {
        fputs("VirtualAllocEx failed.\n", stderr);
        return -1;
    }

    thread_proc = GetProcAddress(LoadLibraryA("kernel32.dll"), "LoadLibraryA");
    if (thread_proc == NULL)
    {
        fputs("Failed to find LoadLibraryA address.\n", stderr);
        return -1;
    }

    t = WriteProcessMemory(
        parent_process,
        buffer,
        dll_path,
        dll_path_size,
        NULL
    );
    if (t == FALSE)
    {
        fputs("WriteProcessMemory failed.\n", stderr);
        return -1;
    }
	
#ifdef CLINK_DEBUG
    printf("DLL to inject: %s\n", dll_path);
    printf("Creating remote thread at: %p with param %p\n", thread_proc, buffer);
#endif

    toggle_threads(parent_pid, 0);
    remote_thread = CreateRemoteThread(
        parent_process,
        NULL,
        0,
        thread_proc,
        buffer,
        0,
        &thread_id
    );

    // Wait for injection to happen.
    WaitForSingleObject(remote_thread, INFINITE);
    toggle_threads(parent_pid, 1);

    VirtualFreeEx(parent_process, buffer, 0, MEM_RELEASE);
    CloseHandle(parent_process);
    return 0;
}
