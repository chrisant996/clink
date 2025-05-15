// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host/host_lua.h"
#include "utils/app_context.h"
#include "utils/usage.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <lua/lua_script_loader.h>
#include <terminal/terminal.h>
#include <terminal/terminal_out.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>

extern "C" {
#include <lua.h>
}

#include <getopt.h>

//------------------------------------------------------------------------------
extern void host_load_app_scripts(lua_state& lua);
extern bool g_elevated;

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
static bool is_redirected()
{
    DWORD dummy;
    return (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dummy) ||
            !GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &dummy));
}

//------------------------------------------------------------------------------
static bool call_updater(lua_state& lua, bool do_nothing, bool force_prompt)
{
    const bool elevated = os::is_elevated();
    const bool redirected = is_redirected();

    auto app_ctx = app_context::get();
    app_ctx->update_env(); // Set %=clink.bin% so the Lua code can find the exe.

    lua_State *state = lua.get_state();
    save_stack_top ss(state);

    if (do_nothing)
    {
        lua.push_named_function(state, "clink.checkupdate");
        lua_pushboolean(state, redirected);
        lua.pcall_silent(state, 1, 1);

        bool available = lua_toboolean(state, -1);
        return available;
    }

    if (g_elevated)
        force_prompt = false; // Already prompted; don't prompt again.

    lua.push_named_function(state, "clink.updatenow");
    lua_pushboolean(state, elevated);
    lua_pushboolean(state, force_prompt);
    lua_pushboolean(state, redirected);
    lua.pcall_silent(state, 3, 2);

    int32 ok = int32(lua_tointeger(state, -2));
    const char* msg = lua_tostring(state, -1);

    // Check if elevation is needed.
    if (ok < 0)
    {
        if (elevated)
        {
            // Elevated but needs elevation?  Something went wrong.
            ok = false;
        }
        else
        {
            // Rerun with elevation.
            WCHAR file[MAX_PATH * 2];
            DWORD len = GetModuleFileNameW(NULL, file, _countof(file));
            str_moveable s;
            str_moveable profile;
            app_ctx->get_state_dir(profile);
            s.format("--elevated --profile \"%s\" ", profile.c_str());
            wstr_moveable wargs(s.c_str());
            wargs << get_wargs();
            puts("Requesting administrator access to install update...");
            // Weird edge case:  If elevation is triggered because an update
            // is available, but the elevated process detects no update is
            // available, that will be reported as success when in fact
            // nothing was done by this update command.
            ok = (len < _countof(file) - 1 && os::run_as_admin(NULL, file, wargs.c_str()));
            msg = ok ? "updated Clink; update will take effect in new Clink windows." : "update failed; see log file for details.";
        }
    }

    if (!ok && !msg)
        msg = "the update attempt failed for an unknown reason.";

    if (msg && *msg)
    {
        str<> tmp;
        tmp = msg;
        tmp.data()[0] = toupper(tmp.data()[0]);

        if (!redirected)
        {
            str<> colored_msg(ok ? "\x1b[0;1;32m" : "\x1b[m");
            colored_msg << tmp;
            colored_msg << "\x1b[m\n";
            g_printer->print(colored_msg.c_str(), colored_msg.length());
        }
        else
        {
            fprintf(ok ? stdout : stderr, "%s\n", tmp.c_str());
        }

        if (ok)
        {
            lua.push_named_function(state, "clink.printreleasesurl");
            lua.pcall_silent(state, 0, 0);
        }
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
int32 update(int32 argc, char** argv)
{
    static const char* help_usage = "Usage: update [options]\n";

    static const struct option options[] = {
        { "help",               no_argument,        nullptr, 'h' },
        { "check",              no_argument,        nullptr, 'n' },
        { "nothing",            no_argument,        nullptr, 'n' },
        { "prompt",             no_argument,        nullptr, '|' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-h, --help",               "Shows this help text.",
        "-n, --check",              "Do nothing; check for an update, but don't install it.",
        nullptr
    };

    terminal term = terminal_create();
    printer printer(*term.out);
    printer_context printer_context(term.out, &printer);

    // Parse arguments
    DWORD target_pid = 0;
    app_context::desc app_desc;
    int32 i;
    int32 ret = 1;
    bool is_autorun = false;
    bool do_nothing = false;
    bool force_prompt = false;
    while ((i = getopt_long(argc, argv, "?hn", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 'n':
            do_nothing = true;
            break;
        case '|':
            force_prompt = true;
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
                "Checks for an updated version of Clink and installs it.\n"
                "\n"
                "The updater requires PowerShell.\n");
            return ret;
        }
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
    const bool ok = call_updater(lua, do_nothing, force_prompt);
    ret = !ok; // Return 0 on success.

    return ret;
}
