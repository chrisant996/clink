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

#include "pch.h"
#include "shared/pe.h"
#include "shared/util.h"
#include "shared/shared_mem.h"
#include "dll/inject_args.h"

#define CLINK_DLL_NAME "clink_dll_" AS_STR(PLATFORM) ".dll"

int do_inject_impl(DWORD, const char*);

//------------------------------------------------------------------------------
static int check_dll_version(const char* clink_dll)
{
    char buffer[1024];
    VS_FIXEDFILEINFO* file_info;
    int error = 0;

    if (GetFileVersionInfo(clink_dll, 0, sizeof(buffer), buffer) != TRUE)
    {
        return 0;
    }

    if (VerQueryValue(buffer, "\\", (void**)&file_info, NULL) != TRUE)
    {
        return 0;
    }

    LOG_INFO("DLL version: %08x %08x",
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
    HANDLE kernel32;
    SYSTEM_INFO sys_info;
    OSVERSIONINFOEX osvi;
    char* slash;

    ret = 0;
    kernel32 = LoadLibraryA("kernel32.dll");

    GetSystemInfo(&sys_info);

    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionEx((void*)&osvi);

#ifdef __MINGW32__
    typedef BOOL (WINAPI *_IsWow64Process)(HANDLE, BOOL*);
    _IsWow64Process IsWow64Process = (_IsWow64Process)GetProcAddress(
        kernel32,
        "IsWow64Process"
    );
#endif // __MINGW32__

    // Get path to clink's DLL that we'll inject.
    GetModuleFileName(NULL, dll_path, sizeof(dll_path));
    slash = strrchr(dll_path, '\\');
    if (slash != NULL)
    {
        *(slash + 1) = '\0';
    }
    strcat(dll_path, CLINK_DLL_NAME);

    // Reset log file, start logging!
    LOG_INFO(NULL);
    LOG_INFO("System: ver=%d.%d %d.%d arch=%d cpus=%d cpu_type=%d page_size=%d",
        osvi.dwMajorVersion,
        osvi.dwMinorVersion,
        osvi.wServicePackMajor,
        osvi.wServicePackMinor,
        sys_info.wProcessorArchitecture,
        sys_info.dwNumberOfProcessors,
        sys_info.dwProcessorType,
        sys_info.dwPageSize
    );
    LOG_INFO("Version: %d.%d.%d",
        CLINK_VER_MAJOR,
        CLINK_VER_MINOR,
        CLINK_VER_POINT
    );
    LOG_INFO("DLL: %s", dll_path);

    LOG_INFO("Parent pid: %d", target_pid);

    // Check Dll's version.
    if (!check_dll_version(dll_path))
    {
        LOG_ERROR("DLL failed version check.");
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
    if (parent_process == NULL)
    {
        LOG_ERROR("Failed to open parent process.");
        return 0;
    }

    // Check arch matches.
    IsWow64Process(parent_process, is_wow_64);
    IsWow64Process(GetCurrentProcess(), is_wow_64 + 1);
    if (is_wow_64[0] != is_wow_64[1])
    {
        LOG_ERROR("32/64-bit mismatch. Use loader executable that matches parent architecture.");
        return 0;
    }

    // Create a buffer in the process to write data to.
    buffer = VirtualAllocEx(
        parent_process,
        NULL,
        sizeof(dll_path),
        MEM_COMMIT,
        PAGE_READWRITE
    );
    if (buffer == NULL)
    {
        LOG_ERROR("VirtualAllocEx failed");
        return 0;
    }

    // We'll use LoadLibraryA as the entry point for out remote thread.
    thread_proc = LoadLibraryA;
    if (thread_proc == NULL)
    {
        LOG_ERROR("Failed to find LoadLibraryA address");
        return 0;
    }

    // Tell remote process what DLL to load.
    t = WriteProcessMemory(
        parent_process,
        buffer,
        dll_path,
        strlen(dll_path) + 1,
        NULL
    );
    if (t == FALSE)
    {
        LOG_ERROR("WriteProcessMemory() failed");
        return 0;
    }
    
    LOG_INFO("Creating remote thread at %p with parameter %p", thread_proc, buffer);

    // Disable threads and create a remote thread.
    toggle_threads(target_pid, 0);
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
        LOG_ERROR("CreateRemoteThread() failed");
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
        LOG_INFO("Failed to snapshot module state.");
        return 0;
    }

    ret = 0;
    ok = Module32First(th32, &module_entry);
    while (ok != FALSE)
    {
        if (_stricmp(module_entry.szModule, CLINK_DLL_NAME) == 0)
        {
            LOG_INFO("Clink already installed in process.");
            ret = 1;
            break;
        }

        ok = Module32Next(th32, &module_entry);
    }

    CloseHandle(th32);
    return ret;
}

//------------------------------------------------------------------------------
void get_profile_path(const char* in, char* out, int out_size)
{
    if (in[0] == '~' && (in[1] == '\\' || in[1] == '/'))
    {
        char dir[MAX_PATH];

        if (SHGetFolderPath(0, CSIDL_LOCAL_APPDATA, NULL, 0, dir) == S_OK)
        {
            str_cpy(out, dir, out_size);
            str_cat(out, ".", out_size);
            str_cat(out, in + 1, out_size);
            return;
        }
    }

    cpy_path_as_abs(out, in, out_size);
}

//------------------------------------------------------------------------------
int inject(int argc, char** argv)
{
    DWORD target_pid = 0;
    int i;
    int ret = 1;
    int is_autorun = 0;
    shared_mem_t* shared_mem;
    inject_args_t inject_args = { 0 };

    struct option options[] = {
        { "scripts",     required_argument,  NULL, 's' },
        { "profile",     required_argument,  NULL, 'p' },
        { "quiet",       no_argument,        NULL, 'q' },
        { "pid",         required_argument,  NULL, 'd' },
        { "nohostcheck", no_argument,        NULL, 'n' },
        { "ansi",        no_argument,        NULL, 'a' },
        { "nolog",       no_argument,        NULL, 'l' },
        { "autorun",     no_argument,        NULL, '_' },
        { "help",        no_argument,        NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    const char* help[] = {
        "-s, --scripts <path>", "Alternative path to load .lua scripts from.",
        "-p, --profile <path>", "Specifies and alternative path for profile data.",
        "-q, --quiet",          "Suppress copyright output.",
        "-n, --nohostcheck",    "Do not check that host is a supported shell.",
        "-d, --pid <pid>",      "Inject into the process specified by <pid>.",
        "-a, --ansi",           "Target shell uses Windows' ANSI console API.",
        "-l, --nolog",          "Disable file logging.",
        "-h, --help",           "Shows this help text.",
    };

    extern const char* g_clink_header;

    // Parse arguments
    while ((i = getopt_long(argc, argv, "nalqhp:s:d:", options, NULL)) != -1)
    {
        switch (i)
        {
        case 's':
            cpy_path_as_abs(
                inject_args.script_path,
                optarg,
                sizeof_array(inject_args.script_path)
            );
            break;

        case 'p':
            {
                char* buffer = inject_args.profile_path;
                int buffer_size = sizeof_array(inject_args.profile_path);
                get_profile_path(optarg, buffer, buffer_size);
            }
            break;

        case 'n': inject_args.no_host_check = 1; break;
        case 'a': inject_args.ansi_mode = 1;     break;
        case 'q': inject_args.quiet = 1;         break;
        case 'd': target_pid = atoi(optarg);     break;
        case '_': is_autorun = 1;                break;

        case 'l':
            inject_args.no_log = 1;
            disable_log();
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

    // Unless a target pid was specified on the command line, use our parent
    // process pid.
    if (target_pid == 0)
    {
        target_pid = get_parent_pid();
        if (target_pid == -1)
        {
            LOG_ERROR("Failed to find parent process ID.");
            goto end;
        }
    }

    // Check to see if clink is already installed.
    if (is_clink_present(target_pid))
    {
        goto end;
    }

    // Write args to shared memory, inject, and clean up.
    shared_mem = create_shared_mem(1, "clink", target_pid);
    memcpy(shared_mem->ptr, &inject_args, sizeof(inject_args));
    ret = !do_inject(target_pid);
    close_shared_mem(shared_mem);

end:
    return is_autorun ? 0 : ret;
}

// vim: expandtab
