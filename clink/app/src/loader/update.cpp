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
        { "help",        no_argument,        nullptr, 'h' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-h, --help",           "Shows this help text.",
        nullptr
    };

    extern void puts_clink_header();

    // Parse arguments
    DWORD target_pid = 0;
    app_context::desc app_desc;
    int i;
    int ret = 1;
    bool is_autorun = false;
    while ((i = getopt_long(argc, argv, "?h", options, nullptr)) != -1)
    {
        switch (i)
        {
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
                "The updater requires Windows 10 or higher.\n");
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
    const bool ok = call_updater(lua);
    ret = !ok; // Return 0 on success.

    return ret;
}
