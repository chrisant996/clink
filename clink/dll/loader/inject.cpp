// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "paths.h"
#include "process/pe.h"
#include "process/shared_mem.h"
#include "inject_args.h"

#include <core/base.h>
#include <core/log.h>
#include <core/str.h>
#include <getopt.h>

#define CLINK_DLL_NAME "clink_" AS_STR(PLATFORM) ".dll"

int do_inject_impl(DWORD, const char*);

//------------------------------------------------------------------------------
void puts_help(const char**, int);
void cpy_path_as_abs(str_base&, const char*);

//------------------------------------------------------------------------------
static int check_dll_version(const char* clink_dll)
{
    char buffer[1024];
    VS_FIXEDFILEINFO* file_info;
    int error = 0;

    if (GetFileVersionInfo(clink_dll, 0, sizeof(buffer), buffer) != TRUE)
        return 0;

    if (VerQueryValue(buffer, "\\", (void**)&file_info, nullptr) != TRUE)
        return 0;

    LOG("DLL version: %08x %08x",
        file_info->dwFileVersionMS,
        file_info->dwFileVersionLS
    );

    error = (HIWORD(file_info->dwFileVersionMS) != CLINK_VER_MAJOR);
    error = (LOWORD(file_info->dwFileVersionMS) != CLINK_VER_MINOR);
    error = (HIWORD(file_info->dwFileVersionLS) != CLINK_VER_POINT);

    return !error;
}

//------------------------------------------------------------------------------
static DWORD get_parent_pid()
{
    ULONG_PTR pbi[6];
    ULONG size = 0;
    LONG (WINAPI *NtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);

    *(FARPROC*)&NtQueryInformationProcess = GetProcAddress(
        LoadLibraryA("ntdll.dll"),
        "NtQueryInformationProcess"
    );

    if (NtQueryInformationProcess)
    {
        LONG ret = NtQueryInformationProcess(GetCurrentProcess(), 0, &pbi,
            sizeof(pbi), &size);

        if ((ret >= 0) && (size == sizeof(pbi)))
            return (DWORD)(pbi[5]);
    }

    return -1;
}

//------------------------------------------------------------------------------
static void toggle_threads(DWORD pid, int on)
{
    BOOL ok;
    HANDLE th32;
    THREADENTRY32 te = { sizeof(te) };

    th32 = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid);
    if (th32 == INVALID_HANDLE_VALUE)
    {
        return;
    }

    ok = Thread32First(th32, &te);
    while (ok != FALSE)
    {
        HANDLE thread;

        if (te.th32OwnerProcessID != pid)
        {
            ok = Thread32Next(th32, &te);
            continue;
        }

        thread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (on)
        {
            ResumeThread(thread);
        }
        else
        {
            SuspendThread(thread);
        }
        CloseHandle(thread);

        ok = Thread32Next(th32, &te);
    }

    CloseHandle(th32);
}

//------------------------------------------------------------------------------
static int do_inject(DWORD target_pid)
{
    int ret;
    char dll_path[512];
    HMODULE kernel32;
    SYSTEM_INFO sys_info;
    char* slash;

    ret = 0;
    kernel32 = LoadLibraryA("kernel32.dll");

    GetSystemInfo(&sys_info);

    OSVERSIONINFOEX osvi;
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionEx((OSVERSIONINFO*)&osvi);

#ifdef __MINGW32__
    typedef BOOL (WINAPI *_IsWow64Process)(HANDLE, BOOL*);
    _IsWow64Process IsWow64Process = (_IsWow64Process)GetProcAddress(
        kernel32,
        "IsWow64Process"
    );
#endif // __MINGW32__

    // Get path to clink's DLL that we'll inject.
    GetModuleFileName(nullptr, dll_path, sizeof(dll_path));
    slash = strrchr(dll_path, '\\');
    if (slash != nullptr)
    {
        *(slash + 1) = '\0';
    }
    strcat(dll_path, CLINK_DLL_NAME);

    // Reset log file, start logging!
    LOG("System: ver=%d.%d %d.%d arch=%d cpus=%d cpu_type=%d page_size=%d",
        osvi.dwMajorVersion,
        osvi.dwMinorVersion,
        osvi.wServicePackMajor,
        osvi.wServicePackMinor,
        sys_info.wProcessorArchitecture,
        sys_info.dwNumberOfProcessors,
        sys_info.dwProcessorType,
        sys_info.dwPageSize
    );
    LOG("Version: %d.%d.%d",
        CLINK_VER_MAJOR,
        CLINK_VER_MINOR,
        CLINK_VER_POINT
    );
    LOG("DLL: %s", dll_path);

    LOG("Parent pid: %d", target_pid);

    // Check Dll's version.
    if (!check_dll_version(dll_path))
    {
        ERR("DLL failed version check.");
        return 0;
    }

    // Inject Clink DLL.
    if (!do_inject_impl(target_pid, dll_path))
        return 0;

    return 1;
}

//------------------------------------------------------------------------------
int do_inject_impl(DWORD target_pid, const char* dll_path)
{
    HANDLE parent_process;
    BOOL is_wow_64[2];
    DWORD thread_id;
    LPVOID buffer;
    void* thread_proc;
    HANDLE remote_thread;
    BOOL t;
    DWORD thread_ret;
    int ret;

    // Open the process so we can operate on it.
    parent_process = OpenProcess(
        PROCESS_QUERY_INFORMATION|
        PROCESS_CREATE_THREAD|
        PROCESS_VM_OPERATION|
        PROCESS_VM_WRITE|
        PROCESS_VM_READ,
        FALSE,
        target_pid
    );
    if (parent_process == nullptr)
    {
        ERR("Failed to open parent process.");
        return 0;
    }

    // Check arch matches.
    IsWow64Process(parent_process, is_wow_64);
    IsWow64Process(GetCurrentProcess(), is_wow_64 + 1);
    if (is_wow_64[0] != is_wow_64[1])
    {
        ERR("32/64-bit mismatch. Use loader executable that matches parent architecture.");
        return 0;
    }

    // Create a buffer in the process to write data to.
    buffer = VirtualAllocEx(
        parent_process,
        nullptr,
        sizeof(dll_path),
        MEM_COMMIT,
        PAGE_READWRITE
    );
    if (buffer == nullptr)
    {
        ERR("VirtualAllocEx failed");
        return 0;
    }

    // We'll use LoadLibraryA as the entry point for out remote thread.
    thread_proc = LoadLibraryA;
    if (thread_proc == nullptr)
    {
        ERR("Failed to find LoadLibraryA address");
        return 0;
    }

    // Tell remote process what DLL to load.
    t = WriteProcessMemory(
        parent_process,
        buffer,
        dll_path,
        strlen(dll_path) + 1,
        nullptr
    );
    if (t == FALSE)
    {
        ERR("WriteProcessMemory() failed");
        return 0;
    }

    LOG("Creating remote thread at %p with parameter %p", thread_proc, buffer);

    // Disable threads and create a remote thread.
    toggle_threads(target_pid, 0);
    remote_thread = CreateRemoteThread(
        parent_process,
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)thread_proc,
        buffer,
        0,
        &thread_id
    );
    if (remote_thread == nullptr)
    {
        ERR("CreateRemoteThread() failed");
        return 0;
    }

    // Wait for injection to complete.
    WaitForSingleObject(remote_thread, 1000);
    GetExitCodeThread(remote_thread, &thread_ret);
    ret = !!thread_ret;

    toggle_threads(target_pid, 1);

    // Clean up and quit
    CloseHandle(remote_thread);
    VirtualFreeEx(parent_process, buffer, 0, MEM_RELEASE);
    CloseHandle(parent_process);

    if (!ret)
        LOG_ERROR("Failed to inject DLL '%s'", dll_path);

    return ret;
}

//------------------------------------------------------------------------------
static int is_clink_present(DWORD target_pid)
{
    int ret;
    BOOL ok;
    MODULEENTRY32 module_entry;

    HANDLE th32 = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, target_pid);
    if (th32 == INVALID_HANDLE_VALUE)
    {
        LOG("Failed to snapshot module state.");
        return 0;
    }

    ret = 0;
    ok = Module32First(th32, &module_entry);
    while (ok != FALSE)
    {
        if (_stricmp(module_entry.szModule, CLINK_DLL_NAME) == 0)
        {
            LOG("Clink already installed in process.");
            ret = 1;
            break;
        }

        ok = Module32Next(th32, &module_entry);
    }

    CloseHandle(th32);
    return ret;
}

//------------------------------------------------------------------------------
void get_profile_path(const char* in, str_base& out)
{
    if (in[0] == '~' && (in[1] == '\\' || in[1] == '/'))
    {
        char dir[MAX_PATH];
        if (SHGetFolderPath(0, CSIDL_LOCAL_APPDATA, nullptr, 0, dir) == S_OK)
        {
            out << dir << "." << (in + 1);
            return;
        }
    }

    cpy_path_as_abs(out, in);
}

//------------------------------------------------------------------------------
int inject(int argc, char** argv)
{
    struct option options[] = {
        { "profile",     required_argument,  nullptr, 'p' },
        { "quiet",       no_argument,        nullptr, 'q' },
        { "pid",         required_argument,  nullptr, 'd' },
        { "nolog",       no_argument,        nullptr, 'l' },
        { "autorun",     no_argument,        nullptr, '_' },
        { "help",        no_argument,        nullptr, 'h' },
        { nullptr, 0, nullptr, 0 }
    };

    const char* help[] = {
        "-p, --profile <path>", "Specifies and alternative path for profile data.",
        "-q, --quiet",          "Suppress copyright output.",
        "-d, --pid <pid>",      "Inject into the process specified by <pid>.",
        "-l, --nolog",          "Disable file logging.",
        "-h, --help",           "Shows this help text.",
    };

    extern const char* g_clink_header;

    // Parse arguments
    bool is_autorun = false;
    DWORD target_pid = 0;
    inject_args_t inject_args = { 0 };
    int i;
    while ((i = getopt_long(argc, argv, "nalqhp:d:", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 'p':
            {
                char* data = inject_args.profile_path;
                int size = sizeof_array(inject_args.profile_path);
                str_base buffer(data, size);
                get_profile_path(optarg, buffer);
            }
            break;

        case 'q': inject_args.quiet = 1;         break;
        case 'd': target_pid = atoi(optarg);     break;
        case '_': is_autorun = true;             break;

        case 'l':
            inject_args.no_log = 1;
            break;

        case '?':
            goto end;

        case 'h':
        default:
            puts(g_clink_header);
            puts_help(help, sizeof_array(help));
            goto end;
        }
    }

    // Restart the log file on every inject.
    str<256> log_path;
    get_log_dir(log_path);
    log_path << "/clink.log";
    unlink(log_path.c_str());

    // Unless a target pid was specified on the command line, use our parent
    // process pid.
    if (target_pid == 0)
    {
        target_pid = get_parent_pid();
        if (target_pid == -1)
        {
            LOG("Failed to find parent pid.");
            goto end;
        }
    }

    // Check to see if clink is already installed.
    if (is_clink_present(target_pid))
        goto end;

    // Write args to shared memory, inject, and clean up.
    shared_mem_t* shared_mem = create_shared_mem(1, "clink", target_pid);
    memcpy(shared_mem->ptr, &inject_args, sizeof(inject_args));
    int ret = !do_inject(target_pid);
    close_shared_mem(shared_mem);

end:
    return is_autorun ? 0 : ret;
}
