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
#include "clink_util.h"

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

    LOG_INFO(NULL);
    LOG_INFO("DLL: %s", dll_path);

    parent_pid = get_parent_pid();
    if (parent_pid == -1)
    {
        LOG_ERROR("Failed to find parent pid.");
        return -1;
    }

    LOG_INFO("Parent pid: %d", parent_pid);

    parent_process = OpenProcess(
        PROCESS_QUERY_INFORMATION|
        PROCESS_CREATE_THREAD|
        PROCESS_VM_OPERATION|
        PROCESS_VM_WRITE|
        PROCESS_VM_READ,
        FALSE,
        parent_pid
    );
    if (parent_process == NULL)
    {
        LOG_ERROR("Failed to open parent process.");
        return -1;
    }

    IsWow64Process(parent_process, is_wow_64);
    IsWow64Process(GetCurrentProcess(), is_wow_64 + 1);
    if (is_wow_64[0] != is_wow_64[1])
    {
        LOG_ERROR("32/64-bit mismatch. Use loader executable that matches parent architecture.");
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
        LOG_ERROR("VirtualAllocEx failed");
        return -1;
    }

    thread_proc = GetProcAddress(LoadLibraryA("kernel32.dll"), "LoadLibraryA");
    if (thread_proc == NULL)
    {
        LOG_ERROR("Failed to find LoadLibraryA address.");
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
        LOG_ERROR("WriteProcessMemory() failed.");
        return -1;
    }
    
    LOG_INFO("Creating remote thread at %p with parameter %p", thread_proc, buffer);

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
    if (remote_thread == NULL)
    {
        LOG_ERROR("CreateRemoteThread() failed.");
        return -1;
    }

    // Wait for injection to complete.
    WaitForSingleObject(remote_thread, INFINITE);
    toggle_threads(parent_pid, 1);

    VirtualFreeEx(parent_process, buffer, 0, MEM_RELEASE);
    CloseHandle(parent_process);
    return 0;
}
