// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "dll/dll.h"
#include "utils/app_context.h"
#include "utils/usage.h"
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

#include <memory>

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
        ERR("Unable to get temp path");
        return;
    }

    target_path << "clink\\dll_cache\\" CLINK_VERSION_STR;

    str<12, false> path_salt;
    path_salt.format("_%08x", str_hash(dll_path.c_str()));
    target_path << path_salt;

    if (!os::make_dir(target_path.c_str()))
    {
        ERR("Unable to create path '%s'", target_path.c_str());
        return;
    }

    target_path << "\\" CLINK_DLL;

#if !defined(CLINK_FINAL)
    // The DLL id only changes on a commit-premake cycle. During development
    // this doesn't work so well so we'll force it through.
    // TODO: check timestamps and only force when timestamp differs.
    bool always = true;
#else
    bool always = false;
#endif

    // Write out origin path to a file so we can backtrack from the cached DLL.
    int32 target_length = target_path.length();
    target_path << ".origin";
    if (always || os::get_path_type(target_path.c_str()) != os::path_type_file)
    {
        wstr<280> wcopy_path(target_path.c_str());
        HANDLE out = CreateFileW(wcopy_path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE)
        {
            bool sharing_violation = (GetLastError() == ERROR_SHARING_VIOLATION);
            ERR("Failed to create origin file at '%s'", target_path.c_str());
            if (!always || !sharing_violation)
                return;
            always = false;
        }
        else
        {
            DWORD written;
            WriteFile(out, dll_path.c_str(), dll_path.length(), &written, nullptr);
            CloseHandle(out);
        }
    }

    target_path.truncate(target_length);

    // Copy the DLL.
    if (always ||
        os::get_path_type(target_path.c_str()) != os::path_type_file ||
        is_file_newer(dll_path.c_str(), target_path.c_str()))
    {
        if (!os::copy(dll_path.c_str(), target_path.c_str()))
        {
            bool sharing_violation = (GetLastError() == ERROR_SHARING_VIOLATION);
            ERR("Failed to copy DLL to '%s'", target_path.c_str());
            if (!always || !sharing_violation)
                return;
            always = false;
        }
    }

    // Copy the PDB to make debugging easier.
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

    dll_path = target_path.c_str();
}

//------------------------------------------------------------------------------
static int32 check_dll_version(const char* clink_dll)
{
    wstr<> wclink_dll(clink_dll);
    char buffer[1024];
    if (GetFileVersionInfoW(wclink_dll.c_str(), 0, sizeof(buffer), buffer) != TRUE)
    {
        ERR("Unable to get DLL version for '%s'", clink_dll);
        return 0;
    }

    VS_FIXEDFILEINFO* file_info;
    if (VerQueryValue(buffer, "\\", (void**)&file_info, nullptr) != TRUE)
    {
        ERR("Unable to query DLL version info for '%s'", clink_dll);
        return 0;
    }

    DEFER_LOG("DLL version: %08x %08x",
        file_info->dwFileVersionMS,
        file_info->dwFileVersionLS
    );

    int32 error = 0;
    error = (HIWORD(file_info->dwFileVersionMS) != CLINK_VERSION_MAJOR);
    error = (LOWORD(file_info->dwFileVersionMS) != CLINK_VERSION_MINOR);
    error = (HIWORD(file_info->dwFileVersionLS) != CLINK_VERSION_PATCH);

    return !error;
}

//------------------------------------------------------------------------------
struct wait_monitor : public process_wait_callback
{
    enum wait_operation { wait_inject, wait_initialize };

    wait_monitor(const char* op) : m_op(op) {}

    bool on_waited(DWORD tick_begin, DWORD wait_result) override
    {
        switch (wait_result)
        {
        case WAIT_TIMEOUT:
            {
                static const char c_msg_slow[] =
                    " is taking a long time...";
                static const char c_msg_timed_out[] =
                    " timed out.  An antivirus tool may be blocking Clink.\n"
                    "Consider adding an exception for Clink in the antivirus tool(s) in use.";

                DWORD elapsed = GetTickCount() - tick_begin;
                if (elapsed >= 30 * 1000)
                {
                    LOG("%s%s", m_op, c_msg_timed_out);
                    fprintf(stderr, "\n\n%s%s\n\n", m_op, c_msg_timed_out);
                    break;
                }

                if (elapsed >= 5000)
                {
                    if (m_elapsed < 5000)
                    {
                        LOG("%s%s", m_op, c_msg_slow);
                        fprintf(stderr, "\n\n%s%s", m_op, c_msg_slow);
                    }
                    else
                    {
                        fputc('.', stderr);
                    }
                }

                m_elapsed = elapsed;
            }
            return false; // Continue the wait loop.

        default:
            ERR("WaitForSingleObject returned %d.", wait_result);
            break;
        }

        return true; // Stop the wait loop.
    }

private:
    const char* m_op = "Something";
    DWORD m_elapsed = 0;
};

//------------------------------------------------------------------------------
static remote_result inject_dll(DWORD target_pid, bool is_autorun, bool force_host=false)
{
    // Get path to clink's DLL that we'll inject.  Using _pgmptr favors the
    // path that was actually uses to spawn the clink process.  This works
    // around the fact that scoop tries to control app versions and updates
    // (but scoop can't quite do it correctly if an app has its own updater).
    str<280> dll_path;
    dll_path = _pgmptr;
    if (dll_path.empty() || !strpbrk(dll_path.c_str(), "/\\"))
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

    DEFER_LOG("System: ver=%d.%d %d.%d arch=%d cpus=%d cpu_type=%d page_size=%d",
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
    DEFER_LOG("Version: %s", CLINK_VERSION_STR_WITH_BRANCH);
    DEFER_LOG("Arch: %s", AS_STR(ARCHITECTURE_NAME));
    DEFER_LOG("DLL: %s", dll_path.c_str());

    DEFER_LOG("Parent pid: %d", target_pid);

    // Check Dll's version.
    if (!check_dll_version(dll_path.c_str()))
    {
        LOG("EXE version: %08x %08x", MAKELONG(CLINK_VERSION_MINOR, CLINK_VERSION_MAJOR), MAKELONG(CLINK_VERSION_PATCH, 0));
        fprintf(stderr, "DLL version mismatch.\n");
        return {};
    }

    // Check for supported host (keep in sync with initialise_clink in dll.cpp).
    process cmd_process(target_pid);
    {
        str<> host;
        if (!cmd_process.get_file_name(host))
        {
            ERR("Unable to get host name.");
            return {};
        }

        const char* host_name = path::get_name(host.c_str());
        if (!host_name || stricmp(host_name, "cmd.exe"))
        {
            if (host_name)
            {
                const bool is_tcc = !stricmp(host_name, "tcc.exe");
                const bool is_4nt = !stricmp(host_name, "4nt.exe");
                if (is_tcc || is_4nt)
                {
                    // Take Command (and 4NT) from JPSoft are recognized during
                    // autorun as benignly invalid hosts.  They may invoke the
                    // AutoRun regkey contents and try to inject Clink, which is
                    // just a side effect of their design.  Since the scenario
                    // is well understood, it's reasonable to be silent about it
                    // during autorun.
                    if (is_autorun)
                        return {-1, nullptr};
                    LOG("Host '%s' is not supported; Clink is not compatible with the JPSoft command shells.", host_name);
                    return {};
                }
            }

            LOG("Unknown host '%s'.", host_name ? host_name : "<no name>");
            if (!force_host)
                return {};
        }

        // Can't inject (or get the command line) if the architecture doesn't
        // match.
        if (!cmd_process.is_arch_match())
            return {};

        // Parse cmd.exe command line for /c or /k to determine whether the host
        // is interactive.  Don't waste time injecting a remote thread if Clink
        // will cancel the inject anyway.  This helps make autorun more
        // reasonable to use.
        wstr<> command_line;
        if (!cmd_process.get_command_line(command_line))
        {
            ERR("Unable to get host command line.");
            return {};
        }
        for (const wchar_t* args = command_line.c_str(); args && (args = wcschr(args, '/'));)
        {
            ++args;
            switch (tolower(*args))
            {
            case 'c':
                // Only log this is something else has already gotten logged.
                // The intent is to avoid filling a log file with a ton of these
                // when Clink is configured in the CMD AutoRun regkey.
                if (!logger::can_defer())
                    LOG("Host is not interactive; cancelling inject.");
                return { -1 };
            case 'k':
                args = nullptr;
                break;
            }
        }
    }

    // Inject Clink DLL.
    wait_monitor monitor("Injecting Clink");
    return cmd_process.inject_module(dll_path.c_str(), &monitor);
}

//------------------------------------------------------------------------------
static bool is_clink_present(DWORD target_pid)
{
    HANDLE th32 = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, target_pid);
    if (th32 == INVALID_HANDLE_VALUE)
    {
        ERR("Failed to snapshot module state.");
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
    for (int32 pid = process().get_parent_pid(); pid;)
    {
        process process(pid);
        process.get_file_name(buffer);
        const char* name = path::get_name(buffer.c_str());
        if (_stricmp(name, "cmd.exe") == 0)
            return pid;
        if (_stricmp(name, "tcc.exe") == 0 || _stricmp(name, "4nt.exe"))
            return pid;

        pid = process.get_parent_pid();
    }

    return 0;
}

//------------------------------------------------------------------------------
void get_profile_path(const char* in, str_base& out)
{
    // Work around completion issue with clink.bat:
    // `clink --profile \foo\` turns into `--profile "\foo\"` which turns into
    // `c:\foo"` as the profile directory.
    str<> _in;
    {
        const char* quote = strchr(in, '"');
        if (quote && quote[1] == '\0')
        {
            _in.concat(in, uint32(quote - in));
            in = _in.c_str();
        }
    }

    // QUIRK:  --profile expanded tilde to %HOME%\AppData\Local, so to keep
    // older installations working upon upgrade, Clink is locked into that
    // strange and inconsistent behavior.
    if (!path::tilde_expand(in, out, true/*use_appdata_local*/))
    {
        os::get_current_dir(out);
        path::append(out, in);
        path::normalise(out);
    }

    _in = out.c_str();
    os::get_full_path_name(_in.c_str(), out);
}

//------------------------------------------------------------------------------
class injection_error_reporter
{
public:
    injection_error_reporter(int32 target_pid, const char* log_path)
        : m_target_pid(target_pid), m_log_path(log_path)
    {
    }

    ~injection_error_reporter()
    {
        if (!m_ok)
        {
            static const char c_msg_pid[] = "Unable to inject Clink in process id %d.\n";
            static const char c_msg_nopid[] = "Unable to inject Clink.\n";
            fprintf(stderr, m_target_pid ? c_msg_pid : c_msg_nopid, m_target_pid);

            static const char c_msg_enable_log[] = "Enable logging for details.\n";
            static const char c_msg_see_log[] = "See log file for details (%s).\n";
            fprintf(stderr, m_log_path ? c_msg_see_log : c_msg_enable_log, m_log_path);
        }
    }

    void set_ok() { m_ok = true; }

private:
    bool m_ok = false;
    int32 m_target_pid = 0;
    const char* m_log_path = nullptr;
};

//------------------------------------------------------------------------------
const int32 exit_code_success = 0;
const int32 exit_code_nonfatal = 1;
const int32 exit_code_fatal = 2;

//------------------------------------------------------------------------------
int32 inject(int32 argc, char** argv, app_context::desc& app_desc)
{
    // Autorun injection must always return success otherwise it interferes with
    // other scripts (e.g. VS postbuild steps, which causes CMake to be unable
    // to build anything).  https://github.com/mridgers/clink/issues/373

    static const char* help_usage = "Usage: inject [options]\n";

    static const struct option options[] = {
        { "scripts",     required_argument,  nullptr, 's' },
        { "profile",     required_argument,  nullptr, 'p' },
        { "quiet",       no_argument,        nullptr, 'q' },
        { "pid",         required_argument,  nullptr, 'd' },
        { "nolog",       no_argument,        nullptr, 'l' },
        { "help",        no_argument,        nullptr, 'h' },
        // Undocumented flags.
        { "autorun",     no_argument,        nullptr, '_' },
        { "detours",     no_argument,        nullptr, '^' },
        { "forcehost",   no_argument,        nullptr, '|' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-s, --scripts <path>", "Alternative path to load .lua scripts from.",
        "-p, --profile <path>", "Specifies an alternative path for profile data.",
        "-q, --quiet",          "Suppress copyright output.",
        "-d, --pid <pid>",      "Inject into the process specified by <pid>.",
        "-l, --nolog",          "Disable file logging.",
        "-h, --help",           "Shows this help text.",
        nullptr
    };

    // Parse arguments
    DWORD target_pid = 0;
    int32 i;
    int32 ret = exit_code_fatal;
    bool is_autorun = false;
    while ((i = getopt_long(argc, argv, "?lqhp:s:d:", options, nullptr)) != -1)
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

        case 'd': target_pid = atoi(optarg);    break;
        case 'q': app_desc.quiet = true;        break;
        case 'l': app_desc.log = false;         break;
        case '^': app_desc.detours = true;      break;
        case '|': app_desc.force = true;        break;
        case '_': ret = 0; is_autorun = true;   break;

        case '?':
        case 'h':
            // Note: getopt returns '?' on invalid flags, making '-?'
            // indistinguishable from an invalid flag.  So invalid flags end
            // up returning 0 even though ideally they'd return 2.
            ret = exit_code_success;
            // fall through
        default:
            puts_clink_header();
            puts(help_usage);
            puts("Options:");
            puts_help(help);
            puts("When installed for autorun, the automatic inject can be overridden by\n"
                 "setting the CLINK_NOAUTORUN environment variable (to any value).\n"
                 "\n"
                 "The exit code from inject is 0 if successful, 2 if a fatal error occurred,\n"
                 "or 1 if a non-fatal error occurred (such as Clink was already present).");
            return ret;
        }
    }

    // Cancel autorun if CLINK_NOAUTORUN is set.
    str<32> noautorun;
    if (is_autorun && os::get_env("clink_noautorun", noautorun))
    {
#if 0
        DWORD dummy;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (GetConsoleMode(h, &dummy))
        {
            // Only output this message when input has not been redirected, so
            // that this doesn't interfere with scripted usage.
            static const char c_msg[] = "Clink autorun is disabled by CLINK_NOAUTORUN.\n";
            wstr<> wmsg(c_msg);
            DWORD written;
            WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), wmsg.c_str(), wmsg.length(), &written, nullptr);
        }
#endif
        return exit_code_nonfatal;
    }

    std::unique_ptr<app_context> context = std::make_unique<app_context>(app_desc);

    // Start a log file.
    str<256> log_path;
    if (app_desc.log)
    {
        app_context::get()->get_log_path(log_path);

        // Don't restart the log file; append to whatever log file may already
        // exist.  The DLL code restarts the log file on a successful inject.
        new file_logger(log_path.c_str());

        SYSTEMTIME now;
        GetLocalTime(&now);
        DEFER_LOG("---- %04u/%02u/%02u %02u:%02u:%02u.%03u -------------------------------------------------",
            now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        DEFER_LOG("Injecting Clink...");
    }

    injection_error_reporter errrep(target_pid, app_desc.log ? log_path.c_str() : nullptr);

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

    // Does the process exist?
    {
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target_pid);
        if (!handle)
            ERR("Failed to open process %d.", target_pid);
        else
            CloseHandle(handle);
        if (!handle)
            return ret;
    }

    // Check to see if clink is already installed.
    if (is_clink_present(target_pid))
    {
        errrep.set_ok();
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

            string_struct value_scripts = {};
            string_struct value_state = {};
            wstr_base script_path(value_scripts.s);
            wstr_base state_dir(value_state.s);
            to_utf16(script_path, app_desc.script_path);
            to_utf16(state_dir, app_desc.state_dir);

            str<> name;
            name.format("Setting var in process %d", target_pid);
            wait_monitor monitor(name.c_str());
            process(target_pid).remote_callex(func, &monitor, L"=clink.scripts.inject", value_scripts);
            process(target_pid).remote_callex(func, &monitor, L"=clink.profile.inject", value_state);
        }
        else if (!is_autorun)
        {
            fprintf(stderr, "Clink already loaded in process %d.\n", target_pid);
        }
        ret = exit_code_nonfatal;
        return ret;
    }

    // Inject Clink's DLL
    remote_result remote_dll_base = inject_dll(target_pid, is_autorun, app_desc.force);
    if (remote_dll_base.ok <= 0)
    {
        if (remote_dll_base.ok < 0)
        {
            errrep.set_ok();
            ret = exit_code_success;
        }
        return ret;
    }

    if (remote_dll_base.result == nullptr)
    {
        LOG("Process %d was unable to load the Clink DLL.\n", target_pid);
        return ret;
    }

    DEFER_LOG("Initializing Clink...");

    // Remotely call Clink's initialisation function.
    void* our_dll_base = vm().get_alloc_base((void*)"");
    uintptr_t init_func = uintptr_t(remote_dll_base.result);
    init_func += uintptr_t(initialise_clink) - uintptr_t(our_dll_base);
    wait_monitor monitor("Initializing Clink");
    remote_result rr = process(target_pid).remote_callex((process::funcptr_t)init_func, &monitor, app_desc);
    if (!rr.ok)
        return ret;

    // If host validation fails when autorun, then don't report that as a
    // failure since it's an expected and common case.
    if (INT_PTR(rr.result) > 0)       // Success.
        ret = exit_code_success;
    else if (INT_PTR(rr.result) < 0)  // Ignorable failure; don't report error.
        ret = exit_code_success;
    else                              // Failure.
        ret = exit_code_fatal;

    if (!ret)
        errrep.set_ok();

    return is_autorun ? exit_code_success : ret;
}
