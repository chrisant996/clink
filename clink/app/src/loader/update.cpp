// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host/host_lua.h"
#include "utils/app_context.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <lua/lua_script_loader.h>

extern "C" {
#include <lua.h>
}

#include <getopt.h>
#include <shlwapi.h>

//------------------------------------------------------------------------------
extern void host_load_app_scripts(lua_state& lua);
void puts_help(const char* const* help_pairs, const char* const* other_pairs=nullptr);
extern bool g_elevated;

//------------------------------------------------------------------------------
static union {
    FARPROC proc[2];
    struct {
        BOOL (WINAPI* IsUserAnAdmin)();
        BOOL (WINAPI* ShellExecuteExW)(SHELLEXECUTEINFOW* pExecInfo);
    };
} s_shell32;

//------------------------------------------------------------------------------
static bool is_elevation_needed()
{
    HMODULE const hlib = LoadLibrary("shell32.dll");
    if (!hlib)
        return false;

    DLLGETVERSIONPROC pDllGetVersion;
    pDllGetVersion = DLLGETVERSIONPROC(GetProcAddress(hlib, "DllGetVersion"));
    if (!pDllGetVersion)
        return false;

    DLLVERSIONINFO dvi = { sizeof(dvi) };
    HRESULT hr = (*pDllGetVersion)(&dvi);
    if (FAILED(hr))
        return false;

    const DWORD dwVersion = MAKELONG(dvi.dwMinorVersion, dvi.dwMajorVersion);
    if (dwVersion < MAKELONG(0, 5))
        return false;

    s_shell32.proc[0] = GetProcAddress(hlib, "IsUserAnAdmin");
    s_shell32.proc[1] = GetProcAddress(hlib, "ShellExecuteExW");
    return s_shell32.proc[0] && s_shell32.proc[1] && !s_shell32.IsUserAnAdmin();
}

//------------------------------------------------------------------------------
static bool run_as_admin(HWND hwnd, const wchar_t* file, const wchar_t* args)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.hwnd = hwnd;
    sei.fMask = SEE_MASK_FLAG_DDEWAIT|SEE_MASK_FLAG_NO_UI|SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = file;
    sei.lpParameters = args;
    sei.nShow = SW_SHOWNORMAL;

    if (!s_shell32.ShellExecuteExW(&sei) || !sei.hProcess)
        return false;

    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD exitcode = 999;
    if (!GetExitCodeProcess(sei.hProcess, &exitcode))
        exitcode = 1;
    CloseHandle(sei.hProcess);

    return exitcode == 0;
}

//------------------------------------------------------------------------------
static wchar_t* get_wargs()
{
    // Skip arg zero, which is the path to the program being executed.
    wchar_t* wargs = GetCommandLineW();
    if (*wargs == '\"')
    {
        wargs++;
        while (true)
        {
            wchar_t c = *wargs;
            if (!c)
                break;
            if (c == '\"')
            {
                wargs++;
                if (*wargs == ' ' || *wargs == '\t')
                    wargs++;
                break;
            }
            wargs++;
        }
    }
    else
    {
        while (true)
        {
            wchar_t c = *wargs;
            if (!c || c == ' ' || c == '\t')
                break;
            wargs++;
        }
    }
    return wargs;
}

//------------------------------------------------------------------------------
static bool call_updater(lua_state& lua)
{
    const bool elevated = !is_elevation_needed();

    auto app_ctx = app_context::get();
    app_ctx->update_env(); // Set %=clink.bin% so the Lua code can find the exe.

    lua_State *state = lua.get_state();
    save_stack_top ss(state);
    lua.push_named_function(state, "clink.updatenow");
    lua_pushboolean(state, elevated);
    lua.pcall_silent(state, 1, 2);

    int ok = int(lua_tointeger(state, -2));
    const char* msg = lua_tostring(state, -1);

    if (ok < 0)
    {
        if (elevated)
        {
            ok = false;
        }
        else
        {
            WCHAR file[MAX_PATH * 2];
            DWORD len = GetModuleFileNameW(NULL, file, _countof(file));
            str_moveable s;
            str_moveable profile;
            app_ctx->get_state_dir(profile);
            s.format("--elevated --profile \"%s\" ", profile.c_str());
            wstr_moveable wargs(s.c_str());
            wargs << get_wargs();
            ok = (len < _countof(file) - 1 && run_as_admin(NULL, file, wargs.c_str()));
            msg = ok ? "updated Clink." : "update failed; see log file for details.";
        }
    }

    if (!ok && !msg)
        msg = "the update attempt failed for an unknown reason.";

    if (msg && *msg)
    {
        str<> tmp;
        tmp = msg;
        tmp.data()[0] = toupper(tmp.data()[0]);
        fprintf(ok ? stdout : stderr, "%s\n", tmp.c_str());
    }

    if (g_elevated)
    {
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        if (h)
        {
            DWORD mode;
            if (GetConsoleMode(h, &mode) && (mode & (ENABLE_LINE_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_ECHO_INPUT)))
            {
                printf("\nPress any key to continue . . . ");
                _getch();
                puts("");
            }
        }
    }

    return !!ok;
}

//------------------------------------------------------------------------------
int update(int argc, char** argv)
{
    static const char* help_usage = "Usage: update [options]\n";

    static const struct option options[] = {
        { "help",               no_argument,        nullptr, 'h' },
        { "allow-automatic",    no_argument,        nullptr, 'A' },
        { "disallow-automatic", no_argument,        nullptr, 'D' },
        { "allusers",           no_argument,        nullptr, 'a' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-h, --help",               "Shows this help text.",
        "-a, --allusers",           "Modifies automatic updates for all users (requires admin rights).",
        "-A, --allow-automatic",    "Clear registry key that disallows automatic updates.",
        "-D, --disallow-automatic", "Set registry key that disallows automatic updates.",
        nullptr
    };

    extern void puts_clink_header();

    // Parse arguments
    DWORD target_pid = 0;
    app_context::desc app_desc;
    int i;
    int ret = 1;
    bool is_autorun = false;
    bool all_users = false;
    int modify_allow = 0;
    while ((i = getopt_long(argc, argv, "?h", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 'a':
            all_users = true;
            break;
        case 'A':
            modify_allow = 1;
            break;
        case 'D':
            modify_allow = -1;
            break;
        case '?':
        case 'h':
            ret = 0;
            // fall through
        default:
            puts_clink_header();
            puts(help_usage);
            puts("Options:");
            puts_help(help);
            printf(
                "Checks for an updated version of Clink.  If one is available, it is downloaded\n"
                "and will be installed the next time Clink is injected.\n"
                "\n"
                "The --disallow-automatic flag disables automatic updates for all profiles,\n"
                "overriding the 'clink.autoupdate' setting.  Adding the --allusers flag affects\n"
                "all users, but requires admin rights.  'clink info' reports whether automatic\n"
                "updates are allowed.\n"
                "\n"
                "The updater requires PowerShell.\n");
            return ret;
        }
    }

    if (modify_allow)
    {
        HKEY hkeyRoot = all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        HKEY hkey;
        DWORD dwDisposition;
        LSTATUS status = RegCreateKeyExW(hkeyRoot, L"Software\\Clink", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, nullptr, &hkey, &dwDisposition);
        if (status == ERROR_SUCCESS)
        {
            DWORD value = (modify_allow < 0);
            status = RegSetValueExW(hkey, L"DisallowAutoUpdate", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
            if (status == ERROR_SUCCESS)
            {
                printf("Automatic updates %s for %s.\n",
                       value ? "disallowed" : "allowed",
                       all_users ? "all users" : "the current user");
                return 0;
            }
        }
        puts("You must have administrator rights to use the --allusers flag.");
        return 1;
    }

    // Start logger; but only append, don't reset the log.
    auto* app_ctx = app_context::get();
    if (app_ctx->is_logging_enabled() && !logger::get())
    {
        str<256> log_path;
        app_ctx->get_log_path(log_path);
        new file_logger(log_path.c_str());
    }

    // Load enough lua state to run the updater.
    host_lua lua;
    host_load_app_scripts(lua);

    // Call the updater.
    const bool ok = call_updater(lua);
    ret = !ok; // Return 0 on success.

    return ret;
}
