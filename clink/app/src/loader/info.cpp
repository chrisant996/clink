// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "version.h"

#include <core/str.h>
#include <core/os.h>
#include <core/path.h>

//------------------------------------------------------------------------------
int clink_info(int argc, char** argv)
{
    struct {
        const char* name;
        void        (app_context::*method)(str_base&) const;
    } infos[] = {
        { "binaries",   &app_context::get_binaries_dir },
        { "state",      &app_context::get_state_dir },
        { "log",        &app_context::get_log_path },
        { "settings",   &app_context::get_settings_path },
        { "history",    &app_context::get_history_path },
    };

    const int spacing = 8;

    // Version information
    printf("%-*s : %s\n", spacing, "version", CLINK_VERSION_STR " (" CLINK_COMMIT ")");

    // Paths
    const auto* context = app_context::get();
    for (const auto& info : infos)
    {
        str<280> out;
        (context->*info.method)(out);
        printf("%-*s : %s\n", spacing, info.name, out.c_str());
    }

    // Inputrc environment variables.
    const char* env_vars[] = {
        "clink_inputrc",
        "userprofile",
        "localappdata",
        "appdata",
        "home"
    };

    bool labeled = false;
    for (const char* env_var : env_vars)
    {
        const char* label = labeled ? "" : "inputrc";
        labeled = true;
        printf("%-*s : %%%s%%\n", spacing, label, env_var);

        str<280> out;
        if (!os::get_env(env_var, out))
        {
            printf("%-*s     (unset)\n", spacing, "");
            continue;
        }

        path::append(out, ".inputrc");
        for (int i = 0; i < 2; ++i)
        {
            printf("%-*s     %s\n", spacing, "", out.c_str());
            int out_length = out.length();
            out.data()[out_length - 8] = '_';
        }
    }

    return 0;
}
