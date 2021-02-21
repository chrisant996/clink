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
#include <process/pe.h>

//------------------------------------------------------------------------------
INT_PTR WINAPI  initialise_clink(const app_context::desc&);
void            puts_help(const char* const*, int);

//------------------------------------------------------------------------------
static bool get_file_info(const wchar_t* file, FILETIME& ft, ULONGLONG& size)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(file, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    ft = fd.ftLastWriteTime;
    size = (ULONGLONG(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    FindClose(h);
    return true;
}

//------------------------------------------------------------------------------
static bool is_file_newer(const char* origin, const char* cached)
{
    FILETIME ftO;
    FILETIME ftC;
    ULONGLONG sizeO;
    ULONGLONG sizeC;
    wstr<> tmp;

    tmp = origin;
    if (!get_file_info(tmp.c_str(), ftO, sizeO))
        return false;

    tmp = cached;
    if (!get_file_info(tmp.c_str(), ftC, sizeC))
        return false;

    return CompareFileTime(&ftO, &ftC) > 0 || sizeO != sizeC;
}

//------------------------------------------------------------------------------
static void copy_dll(str_base& dll_path)
{
    str<280> target_path;
    if (!os::get_temp_dir(target_path))
    {
        LOG("Unable to get temp path");
        return;
    }

    target_path << "clink\\dll_cache\\" CLINK_VERSION_STR;

    str<12, false> path_salt;
    path_salt.format("_%08x", str_hash(dll_path.c_str()));
    target_path << path_salt;

    os::make_dir(target_path.c_str());
    if (os::get_path_type(target_path.c_str()) != os::path_type_dir)
    {
        LOG("Unable to create path '%s'", target_path.c_str());
        return;
    }

    target_path << "\\" CLINK_DLL;

#if !defined(CLINK_FINAL)
    // The DLL id only changes on a commit-premake cycle. During development this
    // doesn't work so well so we'll force it through. TODO: check timestamps
    const bool always = true;
#else
    const bool always = false;
#endif

    // Write out origin path to a file so we can backtrack from the cached DLL.
    int target_length = target_path.length();
    target_path << ".origin";
    if (always || os::get_path_type(target_path.c_str()) != os::path_type_file)
    {
        wstr<280> wcopy_path(target_path.c_str());
        HANDLE out = CreateFileW(wcopy_path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            WriteFile(out, dll_path.c_str(), dll_path.length(), &written, nullptr);
            CloseHandle(out);
        }
    }

    if (os::get_path_type(target_path.c_str()) != os::path_type_file)
    {
        LOG("Failed to create origin file at '%s'.", target_path.c_str());
        return;
    }

    target_path.truncate(target_length);

    // Copy the DLL.
    if (always ||
        os::get_path_type(target_path.c_str()) != os::path_type_file ||
        is_file_newer(dll_path.c_str(), target_path.c_str()))
    {
        os::copy(dll_path.c_str(), target_path.c_str());
    }

    if (os::get_path_type(target_path.c_str()) != os::path_type_file)
    {
        LOG("Failed to copy DLL to '%s'", target_path.c_str());
        return;
    }

    // Copy the PDB to make debugging easier.
#ifdef CLINK_DEBUG
    if (dll_path.length() > 4)
    {
        str<280> pdb_path;
        str<280> pdb_target_path;
        pdb_path = dll_path.c_str();
        pdb_target_path = target_path.c_str();
        pdb_path.truncate(pdb_path.length() - 4);
        pdb_target_path.truncate(pdb_target_path.length() - 4);
        pdb_path << ".pdb";
        pdb_target_path << ".pdb";
        if (always || os::get_path_type(pdb_target_path.c_str()) != os::path_type_file)
            os::copy(pdb_path.c_str(), pdb_target_path.c_str());
    }
#endif

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
static DWORD find_inject_target()
{
    str<512, false> buffer;
    for (int pid = process().get_parent_pid(); pid;)
    {
        process process(pid);
        process.get_file_name(buffer);
        const char* name = path::get_name(buffer.c_str());
        if (_stricmp(name, "cmd.exe") == 0)
            return pid;

        pid = process.get_parent_pid();
    }

    return 0;
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
    // Autorun injection must always return success otherwise it interferes with
    // other scripts (e.g. VS postbuild steps, which causes CMake to be unable
    // to build anything).  https://github.com/mridgers/clink/issues/373

    static const struct option options[] = {
        { "scripts",     required_argument,  nullptr, 's' },
        { "profile",     required_argument,  nullptr, 'p' },
        { "quiet",       no_argument,        nullptr, 'q' },
        { "pid",         required_argument,  nullptr, 'd' },
        { "nolog",       no_argument,        nullptr, 'l' },
        { "autorun",     no_argument,        nullptr, '_' },
        { "help",        no_argument,        nullptr, 'h' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-s, --scripts <path>", "Alternative path to load .lua scripts from.",
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
    int ret = 1;
    bool is_autorun = false;
    while ((i = getopt_long(argc, argv, "lqhp:s:d:", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 's':
            {
                str<> arg(optarg);
                arg.trim();
                str_base script_path(app_desc.script_path);
                os::get_current_dir(script_path);
                path::append(script_path, arg.c_str());
                path::normalise(script_path);
            }
            break;

        case 'p':
            {
                str<> arg(optarg);
                arg.trim();
                str_base state_dir(app_desc.state_dir);
                get_profile_path(arg.c_str(), state_dir);
            }
            break;

        case 'q': app_desc.quiet = true;        break;
        case 'd': target_pid = atoi(optarg);    break;
        case '_': ret = 0; is_autorun = true;   break;

        case 'l':
            app_desc.log = false;
            break;

        case '?':
            return ret;

        case 'h':
            ret = 0;
            // fall through
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

    // Unless a target pid was specified on the command line search for a
    // compatible parent process.
    if (target_pid == 0)
    {
        if (!(target_pid = find_inject_target()))
        {
            LOG("Failed to find parent pid.");
            return ret;
        }
    }

    // Check to see if clink is already installed.
    if (is_clink_present(target_pid))
    {
        if (app_desc.script_path[0])
        {
            // Get the address to SetEnvironmentVariableW directly from
            // kernel32.dll's export table.  If our import table has had
            // SetEnvironmentVariableW hooked then we'd get a potentially
            // invalid address if we were to just use &SetEnvironmentVariableW.
            pe_info kernel32(LoadLibrary("kernel32.dll"));
            pe_info::funcptr_t func = kernel32.get_export("SetEnvironmentVariableW");

            struct string_struct
            {
                wchar_t s[sizeof_array(app_desc.script_path)];
            };

            string_struct value = {};
            wstr_base script_path(value.s);
            to_utf16(script_path, app_desc.script_path);

            process(target_pid).remote_call(func, L"=clink.scripts.inject", value);
        }
        return ret;
    }

    // Inject Clink's DLL
    void* remote_dll_base = inject_dll(target_pid);
    if (remote_dll_base == nullptr)
        return ret;

    // Remotely call Clink's initialisation function.
    void* our_dll_base = vm().get_alloc_base("");
    uintptr_t init_func = uintptr_t(remote_dll_base);
    init_func += uintptr_t(initialise_clink) - uintptr_t(our_dll_base);
    ret = (process(target_pid).remote_call((process::funcptr_t)init_func, app_desc) == nullptr);

    return is_autorun ? 0 : ret;
}
