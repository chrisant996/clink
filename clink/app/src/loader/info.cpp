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
    static const struct {
        const char* name;
        void        (app_context::*method)(str_base&) const;
        bool        suppress_when_empty;
    } infos[] = {
        { "binaries",   &app_context::get_binaries_dir },
        { "state",      &app_context::get_state_dir },
        { "log",        &app_context::get_log_path },
        { "settings",   &app_context::get_settings_path },
        { "history",    &app_context::get_history_path },
        { "scripts",    &app_context::get_script_path_readable, true/*suppress_when_empty*/ },
    };

    const auto* context = app_context::get();
    const int spacing = 8;

    // Version information
    printf("%-*s : %s\n", spacing, "version", CLINK_VERSION_STR);
    printf("%-*s : %d\n", spacing, "session", context->get_id());

    // Paths
    for (const auto& info : infos)
    {
        str<280> out;
        (context->*info.method)(out);
        if (!info.suppress_when_empty || !out.empty())
            printf("%-*s : %s\n", spacing, info.name, out.c_str());
    }

    // Inputrc environment variables.
    static const char* const env_vars[] = {
        "clink_inputrc",
        "userprofile",
        "localappdata",
        "appdata",
        "home"
    };

    // Inputrc file names.
    static const char *const file_names[] = {
        ".inputrc",
        "_inputrc",
        "clink_inputrc",
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

        int base_len = out.length();

        for (int i = 0; i < sizeof_array(file_names); ++i)
        {
            out.truncate(base_len);
            path::append(out, file_names[i]);

            bool exists = os::get_path_type(out.c_str()) == os::path_type_file;

            if (exists || i < 2)
                printf("%-*s     %s%s\n", spacing, "", out.c_str(), exists ? "   (exists)" : "");
        }
    }

    return 0;
}
