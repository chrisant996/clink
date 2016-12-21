// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "version.h"

#include <core/str.h>

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

    const char* format = "%8s : %s\n";

    const auto* context = app_context::get();
    for (const auto& info : infos)
    {
        str<280> out;
        (context->*info.method)(out);
        printf(format, info.name, out.c_str());
    }

    printf(format, "version", CLINK_VERSION_STR " (" CLINK_COMMIT ")");

    return 0;
}
