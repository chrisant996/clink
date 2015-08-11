// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "process.h"

#include <core/path.h>
#include <core/str.h>
#include <process/vm.h>
#include <process/pe.h>
#include <PsApi.h>
#include <TlHelp32.h>

//------------------------------------------------------------------------------
process::process(int pid)
: m_pid(pid)
{
    if (m_pid == -1)
        m_pid = GetCurrentProcessId();
}

//------------------------------------------------------------------------------
int process::get_parent_pid() const
{
    LONG (WINAPI *NtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);

    *(FARPROC*)&NtQueryInformationProcess = GetProcAddress(
        LoadLibraryA("ntdll.dll"),
        "NtQueryInformationProcess"
    );

    if (NtQueryInformationProcess != nullptr)
    {
        ULONG size = 0;
        ULONG_PTR pbi[6];
        LONG ret = NtQueryInformationProcess(GetCurrentProcess(), 0, &pbi,
            sizeof(pbi), &size);

        if ((ret >= 0) && (size == sizeof(pbi)))
            return (DWORD)(pbi[5]);
    }

    return 0;
}

//------------------------------------------------------------------------------
bool process::get_file_name(str_base& out) const
{
    Handle handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    if (!handle)
        return false;

    DWORD out_chars = out.size();
    BOOL ok = QueryFullProcessImageName(handle, 0, out.data(), &out_chars);

    return !!ok;
}

//------------------------------------------------------------------------------
process::arch process::get_arch() const
{
    int is_x64_os;
    SYSTEM_INFO system_info;
    GetNativeSystemInfo(&system_info);
    is_x64_os = system_info.wProcessorArchitecture;
    is_x64_os = (is_x64_os == PROCESSOR_ARCHITECTURE_AMD64);

    if (is_x64_os)
    {
        Handle handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
        if (!handle)
            return arch_unknown;

        BOOL is_wow64;
        if (IsWow64Process(handle, &is_wow64) == FALSE)
            return arch_unknown;

        return is_wow64 ? arch_x86 : arch_x64;
    }

    return arch_x86;
}

//------------------------------------------------------------------------------
void process::pause_impl(bool suspend)
{
    Handle th32 = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, m_pid);
    if (!th32)
        return;

    THREADENTRY32 te = { sizeof(te) };
    BOOL ok = Thread32First(th32, &te);
    while (ok != FALSE)
    {
        if (te.th32OwnerProcessID == m_pid)
        {
            Handle thread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
            suspend ? SuspendThread(thread) : ResumeThread(thread);
        }

        ok = Thread32Next(th32, &te);
    }
}

//------------------------------------------------------------------------------
bool process::inject_module(const char* dll_path)
{
    // Check we can inject into the target.
    if (process().get_arch() < get_arch())
        return false;

    // Create a buffer in the process to write data to.
    vm_access target_vm(m_pid);
    void* buffer = target_vm.alloc(sizeof(dll_path));
    if (buffer == nullptr)
        return false;

    target_vm.write(buffer, dll_path, strlen(dll_path) + 1);

    int thread_ret = 0;

    // Get the address to LoadLibrary. Note that we do with without using any
    // Windows API calls in case someone's hook LoadLibrary. We'd get the wrong
    // address. Address are the same across processes.
    pe_info kernel32(LoadLibrary("kernel32.dll"));
    void* thread_proc = kernel32.get_export("LoadLibraryA");
    if (thread_proc != nullptr)
        thread_ret = remote_call(thread_proc, buffer);

    // Clean up and quit
    target_vm.free(buffer);
    return (thread_ret != 0);
}

//------------------------------------------------------------------------------
int process::remote_call(void* function, void* param)
{
    // Open the process so we can operate on it.
    Handle handle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_CREATE_THREAD,
        FALSE, m_pid);
    if (!handle)
        return false;

    pause();

    // The 'remote call' is actually a thread that's created in the process and
    // and then waited on for completion.
    DWORD thread_id;
    Handle remote_thread = CreateRemoteThread(handle, nullptr, 0,
        (LPTHREAD_START_ROUTINE)function, param, 0, &thread_id);
    if (!remote_thread)
    {
        unpause();
        return 0;
    }

    // Wait for injection to complete.
    DWORD thread_ret;
    WaitForSingleObject(remote_thread, INFINITE);
    GetExitCodeThread(remote_thread, &thread_ret);

    unpause();

    return thread_ret;
}
