// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "version.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_hash.h>
#include <getopt.h>
#include <process/process.h>
#include <process/vm.h>

//------------------------------------------------------------------------------
bool    initialise_clink(const app_context::desc&);
void    puts_help(const char**, int);

//------------------------------------------------------------------------------
static void copy_dll(str_base& dll_path)
{
    str<280> target_path;
    if (!os::get_temp_dir(target_path))
    {
        LOG("Unable to get temp path");
        return;
    }

    target_path << "/clink/dll_cache";
    if (!os::make_dir(target_path.c_str()))
    {
        LOG("Unable to create path '%s'", target_path.c_str());
        return;
    }

    char number[16];
    str_base(number).format("/%x", process().get_pid());
    str<280> temp_path;
    temp_path << target_path.c_str();
    temp_path << number;

    unsigned int dll_id = str_hash(AS_STR(ARCHITECTURE) CLINK_VERSION_STR);
    str_base(number).format("%x", dll_id);
    target_path << "/clink_" << number << ".dll";

#if !defined(CLINK_FINAL)
    // The DLL id only changes on a commit-premake cycle. During development this
    // doesn't work so well so we'll force it through. TODO: check timestamps
    const bool always = true;
#else
    const bool always = false;
#endif

    if (always || os::get_path_type(target_path.c_str()) != os::path_type_file)
    {
        os::unlink(temp_path.c_str());
        os::copy(dll_path.c_str(), temp_path.c_str());
        os::move(temp_path.c_str(), target_path.c_str());
    }

    bool ok = (os::get_path_type(target_path.c_str()) == os::path_type_file);

    // Write out origin path to a file so we can backtrack from the cached DLL.
    int target_length = target_path.length();
    target_path << ".origin";
    if (always || os::get_path_type(target_path.c_str()) != os::path_type_file)
    {
        wstr<280> wcopy_path(temp_path.c_str());
        HANDLE out = CreateFileW(wcopy_path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            WriteFile(out, dll_path.c_str(), dll_path.length(), &written, nullptr);
            CloseHandle(out);

            os::move(temp_path.c_str(), target_path.c_str());
        }
    }

    ok &= (os::get_path_type(target_path.c_str()) == os::path_type_file);
    if (!ok)
        return;

    target_path.truncate(target_length);
    dll_path = target_path.c_str();
}

//------------------------------------------------------------------------------
static int check_dll_version(const char* clink_dll)
{
    char buffer[1024];
    if (GetFileVersionInfo(clink_dll, 0, sizeof(buffer), buffer) != TRUE)
        return 0;

    VS_FIXEDFILEINFO* file_info;
    if (VerQueryValue(buffer, "\\", (void**)&file_info, nullptr) != TRUE)
        return 0;

    LOG("DLL version: %08x %08x",
        file_info->dwFileVersionMS,
        file_info->dwFileVersionLS
    );

    int error = 0;
    error = (HIWORD(file_info->dwFileVersionMS) != CLINK_VERSION_MAJOR);
    error = (LOWORD(file_info->dwFileVersionMS) != CLINK_VERSION_MINOR);
    error = (HIWORD(file_info->dwFileVersionLS) != CLINK_VERSION_PATCH);

    return !error;
}

//------------------------------------------------------------------------------
static void* inject_dll(DWORD target_pid)
{
    // Get path to clink's DLL that we'll inject.
    str<280> dll_path;
    process().get_file_name(dll_path);
    path::get_directory(dll_path);
    path::append(dll_path, CLINK_DLL);

    copy_dll(dll_path);

    // Reset log file, start logging!
#if 0
    /* GetVersionEx() is deprecated and the VerifyVersioninfo() replacement
     * is bonkers.
     */
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);

    OSVERSIONINFOEX osvi;
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionEx((OSVERSIONINFO*)&osvi);

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
#endif
    LOG("Version: %s", CLINK_VERSION_STR);
    LOG("Arch: x%s", AS_STR(ARCHITECTURE));
    LOG("DLL: %s", dll_path.c_str());

    LOG("Parent pid: %d", target_pid);

    // Check Dll's version.
    if (!check_dll_version(dll_path.c_str()))
    {
        ERR("DLL failed version check.");
        return 0;
    }

    // Inject Clink DLL.
    process cmd_process(target_pid);
    return cmd_process.inject_module(dll_path.c_str());
}

//------------------------------------------------------------------------------
static bool is_clink_present(DWORD target_pid)
{
    HANDLE th32 = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, target_pid);
    if (th32 == INVALID_HANDLE_VALUE)
    {
        LOG("Failed to snapshot module state.");
        return false;
    }

    bool ret = false;

    MODULEENTRY32 module_entry = { sizeof(module_entry) };
    BOOL ok = Module32First(th32, &module_entry);
    while (ok != FALSE)
    {
        if (_strnicmp(module_entry.szModule, "clink_", 6) == 0)
        {
            LOG("Clink already installed in process.");
            ret = true;
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
        wchar_t dir[MAX_PATH];
        if (SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA, nullptr, 0, dir) == S_OK)
        {
            out = dir;
            out << (in + 1);
            return;
        }
    }

    os::get_current_dir(out);
    path::append(out, in);
    path::normalise(out);
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
    DWORD target_pid = 0;
    app_context::desc app_desc;
    int i;
    int ret = false;
    while ((i = getopt_long(argc, argv, "nalqhp:d:", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 'p':
            {
                str_base state_dir(app_desc.state_dir);
                get_profile_path(optarg, state_dir);
            }
            break;

        case 'q': app_desc.quiet = true;        break;
        case 'd': target_pid = atoi(optarg);    break;
        case '_': ret = true;                   break;

        case 'l':
            app_desc.log = false;
            break;

        case '?':
            return ret;

        case 'h':
        default:
            puts(g_clink_header);
            puts_help(help, sizeof_array(help));
            return ret;
        }
    }

    // Restart the log file on every inject.
    str<256> log_path;
    app_context::get()->get_log_path(log_path);
    unlink(log_path.c_str());

    // Unless a target pid was specified on the command line, use our parent
    // process pid.
    if (target_pid == 0)
    {
        target_pid = process().get_parent_pid();
        if (target_pid == 0)
        {
            LOG("Failed to find parent pid.");
            return ret;
        }
    }

    // Check to see if clink is already installed.
    if (is_clink_present(target_pid))
        return ret;

    // Inject Clink's DLL
    void* remote_dll_base = inject_dll(target_pid);
    if (remote_dll_base == nullptr)
        return ret;

    // Remotely call Clink's initialisation function.
    void* our_dll_base = vm().get_alloc_base("");
    uintptr_t init_func = uintptr_t(remote_dll_base);
    init_func += uintptr_t(initialise_clink) - uintptr_t(our_dll_base);
    ret |= (process(target_pid).remote_call((void*)init_func, app_desc) != nullptr);

    return ret;
}
