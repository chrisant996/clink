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
#include <stddef.h>

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
typedef LONG(NTAPI *NtSuspendProcessFunc)(IN HANDLE ProcessHandle);
typedef LONG(NTAPI *NtResumeProcessFunc)(IN HANDLE ProcessHandle);
void process::pause(bool suspend)
{
    static int s_initialized = 0;
    static NtSuspendProcessFunc ntSuspendProcess = nullptr;
    static NtResumeProcessFunc ntResumeProcess = nullptr;
    if (!s_initialized)
    {
        HMODULE hdll = GetModuleHandle("ntdll.dll");
        ntSuspendProcess = (NtSuspendProcessFunc)GetProcAddress(hdll, "NtSuspendProcess");
        ntResumeProcess = (NtResumeProcessFunc)GetProcAddress(hdll, "NtResumeProcess");
        s_initialized = (ntSuspendProcess && ntResumeProcess) ? 1 : -1;
    }

    if (s_initialized > 0)
    {
        handle h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, m_pid);
        if (suspend)
            ntSuspendProcess(h);
        else
            ntResumeProcess(h);
    }
    else
    {
        handle th32 = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, m_pid);
        if (!th32)
            return;

        THREADENTRY32 te = {sizeof(te)};
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
}

//------------------------------------------------------------------------------
void* process::inject_module(const char* dll_path)
{
    // Check we can inject into the target.
    if (process().get_arch() != get_arch())
        return nullptr;

    // Get the address to LoadLibrary. Note that we get LoadLibrary address
    // directly from kernel32.dll's export table. If our import table has had
    // LoadLibraryW hooked then we'd get a potentially invalid address if we
    // were to just use &LoadLibraryW.
    pe_info kernel32(LoadLibrary("kernel32.dll"));
    pe_info::funcptr_t func = kernel32.get_export("LoadLibraryW");

    wstr<280> wpath(dll_path);
    return remote_call_internal(func, wpath.data(), wpath.length() * sizeof(wchar_t));
}

//------------------------------------------------------------------------------
#if defined(_MSC_VER)
# pragma warning(push)
# pragma warning(disable : 4200)
#endif
struct thunk_data
{
    void*   (WINAPI* func)(void*);
    void*   out;
    char    in[];
};
#if defined(_MSC_VER)
# pragma warning(pop)
#endif

//------------------------------------------------------------------------------
static DWORD WINAPI stdcall_thunk(thunk_data& data)
{
    data.out = data.func(data.in);
    return 0;
}

//------------------------------------------------------------------------------
void* process::remote_call_internal(pe_info::funcptr_t function, const void* param, int param_size)
{
    // Open the process so we can operate on it.
    handle process_handle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_CREATE_THREAD,
        FALSE, m_pid);
    if (!process_handle)
        return nullptr;

    // Scanning for 0xc3 works on 64 bit, but not on 32 bit.  I gave up and just
    // imposed a max size of 64 bytes, since the emited code is around 40 bytes.
    static int thunk_size;
    if (!thunk_size)
        for (const auto* c = (unsigned char*)stdcall_thunk; thunk_size < 64 && ++thunk_size, *c++ != 0xc3;);

    vm vm(m_pid);
    vm::region region = vm.alloc(1, vm::access_write);
    if (region.base == nullptr)
        return nullptr;

    int write_offset = 0;
    const auto& vm_write = [&] (const void* data, int size) {
        void* addr = (char*)region.base + write_offset;
        vm.write(addr, data, size);
        write_offset = (write_offset + size + 7) & ~7;
        return addr;
    };

    vm_write((void*)stdcall_thunk, thunk_size);
    void* thunk_ptrs[2] = { function };
    char* remote_thunk_data = (char*)vm_write(thunk_ptrs, sizeof(thunk_ptrs));
    vm_write(param, param_size);
    vm.set_access(region, vm::access_rwx); // writeable so thunk() can write output.

    static_assert(sizeof(thunk_ptrs) == sizeof(thunk_data), "");
    static_assert((offsetof(thunk_data, in) & 7) == 0, "");

    pause();

    // The 'remote call' is actually a thread that's created in the process and
    // then waited on for completion.
    DWORD thread_id;
    handle remote_thread = CreateRemoteThread(process_handle, nullptr, 0,
        (LPTHREAD_START_ROUTINE)region.base, remote_thunk_data, 0, &thread_id);
    if (!remote_thread)
    {
        unpause();
        return 0;
    }

    WaitForSingleObject(remote_thread, INFINITE);
    unpause();

    void* call_ret = nullptr;
    vm.read(&call_ret, remote_thunk_data + offsetof(thunk_data, out), sizeof(call_ret));
    vm.free(region);

    return call_ret;
}



//------------------------------------------------------------------------------
#if defined(_MSC_VER)
# pragma warning(push)
# pragma warning(disable : 4200)
#endif
struct thunk2_data
{
    void*   (WINAPI* func)(void*, void*);
    void*   out;
    void*   in1;
    void*   in2;
    char    buffer[];
};
#if defined(_MSC_VER)
# pragma warning(pop)
#endif

//------------------------------------------------------------------------------
static DWORD WINAPI stdcall_thunk2(thunk2_data& data)
{
    data.out = data.func(data.in1, data.in2);
    return 0;
}

//------------------------------------------------------------------------------
void* process::remote_call_internal(pe_info::funcptr_t function, const void* param1, int param1_size, const void* param2, int param2_size)
{
    // Open the process so we can operate on it.
    handle process_handle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_CREATE_THREAD,
        FALSE, m_pid);
    if (!process_handle)
        return nullptr;

    // Scanning for 0xc3 works on 64 bit, but not on 32 bit.  I gave up and just
    // imposed a max size of 64 bytes, since the emited code is around 40 bytes.
    static int thunk_size;
    if (!thunk_size)
        for (const auto* c = (unsigned char*)stdcall_thunk2; thunk_size < 64 && ++thunk_size, *c++ != 0xc3;);

    vm vm(m_pid);
    vm::region region = vm.alloc(1, vm::access_write);
    if (region.base == nullptr)
        return nullptr;

    int write_offset = 0;
    const auto& vm_write = [&] (const void* data, int size) {
        void* addr = (char*)region.base + write_offset;
        vm.write(addr, data, size);
        write_offset = (write_offset + size + 7) & ~7;
        return addr;
    };

    vm_write((void*)stdcall_thunk2, thunk_size);

    int offset_ptrs = write_offset;
    void* thunk_ptrs[4] = { function }; // func, out, in1, in2
    char* remote_thunk_data = (char*)vm_write(thunk_ptrs, sizeof(thunk_ptrs));

    void* addr_param1 = vm_write(param1, param1_size);
    void* addr_param2 = vm_write(param2, param2_size);

    write_offset = offset_ptrs + sizeof(void*) * 2; // in1
    vm_write(&addr_param1, sizeof(addr_param1));
    vm_write(&addr_param2, sizeof(addr_param2));

    vm.set_access(region, vm::access_rwx); // writeable so thunk() can write output.

    static_assert(sizeof(thunk_ptrs) == sizeof(thunk2_data), "");
    static_assert((offsetof(thunk2_data, buffer) & 7) == 0, "");

    pause();

    // The 'remote call' is actually a thread that's created in the process and
    // then waited on for completion.
    DWORD thread_id;
    handle remote_thread = CreateRemoteThread(process_handle, nullptr, 0,
        (LPTHREAD_START_ROUTINE)region.base, remote_thunk_data, 0, &thread_id);
    if (!remote_thread)
    {
        unpause();
        return 0;
    }

    WaitForSingleObject(remote_thread, INFINITE);
    unpause();

    void* call_ret = nullptr;
    vm.read(&call_ret, remote_thunk_data + offsetof(thunk2_data, out), sizeof(call_ret));
    vm.free(region);

    return call_ret;
}
