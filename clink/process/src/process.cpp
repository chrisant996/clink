// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "process.h"
#include "vm.h"
#include "pe.h"

#include <core/path.h>
#include <core/str.h>
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
        handle handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
        ULONG size = 0;
        ULONG_PTR pbi[6];
        LONG ret = NtQueryInformationProcess(handle, 0, &pbi, sizeof(pbi), &size);
        if ((ret >= 0) && (size == sizeof(pbi)))
            return (DWORD)(pbi[5]);
    }

    return 0;
}

//------------------------------------------------------------------------------
bool process::get_file_name(str_base& out) const
{
    handle handle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, m_pid);
    if (!handle)
        return false;

    static DWORD (WINAPI *func)(HANDLE, HMODULE, LPTSTR, DWORD) = nullptr;
    if (func == nullptr)
        if (HMODULE psapi = LoadLibrary("psapi.dll"))
            *(FARPROC*)&func = GetProcAddress(psapi, "GetModuleFileNameExA");

    if (func != nullptr)
        return (func(handle, nullptr, out.data(), out.size()) != 0);

    return false;
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
        handle handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
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
    handle th32 = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, m_pid);
    if (!th32)
        return;

    THREADENTRY32 te = { sizeof(te) };
    BOOL ok = Thread32First(th32, &te);
    while (ok != FALSE)
    {
        if (te.th32OwnerProcessID == m_pid)
        {
            handle thread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
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
    vm target_vm(m_pid);
    vm::region region = target_vm.alloc(1);
    if (region.base == nullptr)
        return false;

    target_vm.write(region.base, dll_path, strlen(dll_path) + 1);

    int thread_ret = 0;

    // Get the address to LoadLibrary. Note that we do with without using any
    // Windows API calls in case someone's hook LoadLibrary. We'd get the wrong
    // address. Address are the same across processes.
    pe_info kernel32(LoadLibrary("kernel32.dll"));
    auto* thread_proc = kernel32.get_export("LoadLibraryA");
    if (thread_proc != nullptr)
        thread_ret = remote_call_impl(thread_proc, region.base);

    // Clean up and quit
    target_vm.free(region);
    return (thread_ret != 0);
}

//------------------------------------------------------------------------------
int process::remote_call_impl(funcptr_t function, void* param)
{
    // Open the process so we can operate on it.
    handle process_handle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_CREATE_THREAD,
        FALSE, m_pid);
    if (!process_handle)
        return false;

    pause();

    // The 'remote call' is actually a thread that's created in the process and
    // and then waited on for completion.
    DWORD thread_id;
    handle remote_thread = CreateRemoteThread(process_handle, nullptr, 0,
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
