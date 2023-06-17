// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "version.h"

#include <core/str.h>
#include <core/settings.h>
#include <core/os.h>
#include <core/path.h>
#include <getopt.h>

//------------------------------------------------------------------------------
static void print_info_line(HANDLE h, const char* s)
{
    DWORD dummy;
    if (GetConsoleMode(h, &dummy))
    {
        wstr<> tmp(s);
        DWORD written;
        WriteConsoleW(h, tmp.c_str(), tmp.length(), &written, nullptr);
    }
    else
    {
        printf("%s", s);
    }
}

//------------------------------------------------------------------------------
int32 clink_info(int32 argc, char** argv)
{
    static const struct {
        const char* name;
        void        (app_context::*method)(str_base&) const;
        bool        suppress_when_empty;
    } infos[] = {
        { "binaries",           &app_context::get_binaries_dir },
        { "state",              &app_context::get_state_dir },
        { "log",                &app_context::get_log_path },
        { "default settings",   &app_context::get_default_settings_file, true/*suppress_when_empty*/ },
        { "settings",           &app_context::get_settings_path },
        { "history",            &app_context::get_history_path },
        { "scripts",            &app_context::get_script_path_readable, true/*suppress_when_empty*/ },
        { "default_inputrc",    &app_context::get_default_init_file, true/*suppress_when_empty*/ },
    };

    struct info_output
    {
                        info_output(const char* name, str_moveable&& value) : name(name), value(std::move(value)) {}
        const char*     name;
        str_moveable    value;
    };

    const auto* context = app_context::get();

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    // Load the settings from disk, since script paths are affected by settings.
    str<280> settings_file;
    str<280> default_settings_file;
    app_context::get()->get_settings_path(settings_file);
    app_context::get()->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());

    // Get values to output.
    int32 spacing = 8;
    std::vector<info_output> outputs;
    for (const auto& info : infos)
    {
        str_moveable out;
        (context->*info.method)(out);
        if (!info.suppress_when_empty || !out.empty())
        {
            outputs.emplace_back(info.name, std::move(out));
            spacing = max<int32>(spacing, int32(strlen(info.name)));
        }
    }

    // Version information.
    printf("%-*s : %s\n", spacing, "version", CLINK_VERSION_STR);
    printf("%-*s : %d\n", spacing, "session", context->get_id());

    // Output the values.
    str<> s;
    for (const auto& output : outputs)
    {
        s.clear();
        s.format("%-*s : %s\n", spacing, output.name, output.value.c_str());
        print_info_line(h, s.c_str());
    }

    // Inputrc environment variables.
    static const char* const env_vars[] = {
        "clink_inputrc",
        "", // Magic value handled specially below.
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
    bool first = true;
    for (const char* env_var : env_vars)
    {
        bool use_state_dir = !*env_var;
        const char* label = labeled ? "" : "inputrc";
        labeled = true;
        if (use_state_dir)
            printf("%-*s : %s\n", spacing, label, "state directory");
        else
            printf("%-*s : %%%s%%\n", spacing, label, env_var);

        str<280> out;
        if (use_state_dir)
        {
            app_context::get()->get_state_dir(out);
        }
        else if (!os::get_env(env_var, out))
        {
            printf("%-*s     (unset)\n", spacing, "");
            continue;
        }

        int32 base_len = out.length();

        for (int32 i = 0; i < sizeof_array(file_names); ++i)
        {
            out.truncate(base_len);
            path::append(out, file_names[i]);

            bool exists = os::get_path_type(out.c_str()) == os::path_type_file;

            const char* status;
            if (!exists)
                status = "";
            else if (first)
                status = "   (LOAD)";
            else
                status = "   (exists)";

            if (exists || i < 2)
            {
                s.clear();
                s.format("%-*s     %s%s\n", spacing, "", out.c_str(), status);
                print_info_line(h, s.c_str());
            }

            if (exists)
                first = false;
        }
    }

    str<> state_dir;
    app_context::get()->get_state_dir(state_dir);
    if (os::get_path_type(state_dir.c_str()) == os::path_type_file)
    {
        fprintf(stderr,
                "warning: invalid profile directory '%s'.\n"
                "The profile directory must be a directory, but it is a file.\n",
                state_dir.c_str());
    }

    return 0;
}
