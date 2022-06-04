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

//------------------------------------------------------------------------------
extern void host_load_app_scripts(lua_state& lua);
void puts_help(const char* const* help_pairs, const char* const* other_pairs=nullptr);

//------------------------------------------------------------------------------
static bool call_updater(lua_state& lua)
{
    lua_State *state = lua.get_state();
    save_stack_top ss(state);
    lua.push_named_function(state, "clink.updatenow");
    lua.pcall_silent(state, 0, 2);

    const bool ok = !!lua_toboolean(state, -2);
    const char* msg = lua_tostring(state, -1);
    if (!ok && !msg)
        msg = "the update attempt failed for an unknown reason.";

    str<> tmp;
    tmp = msg;
    tmp.data()[0] = toupper(tmp.data()[0]);
    fputs(tmp.c_str(), ok ? stdout : stderr);
    return ok;
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
