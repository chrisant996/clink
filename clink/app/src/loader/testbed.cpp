// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "dll/dll.h"
#include "loader/loader.h"

#include <core/os.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <lib/line_editor.h>
#include <lib/match_generator.h>
#include <lib/recognizer.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>
#include <lua/lua_task_manager.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <utils/app_context.h>
#include <utils/usage.h>
#include <getopt.h>

//------------------------------------------------------------------------------
static int32 editline()
{
    str_compare_scope _(str_compare_scope::relaxed, false/*fuzzy_accent*/);

    // Load the settings from disk, since terminal I/O is affected by settings.
    str<280> settings_file;
    str<280> default_settings_file;
    app_context::get()->get_settings_path(settings_file);
    app_context::get()->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());

    terminal term = terminal_create();
    printer printer(*term.out);
    printer_context prt(term.out, &printer);
    console_config cc;

    lua_state lua;
    lua_match_generator lua_generator(lua);

    line_editor::desc desc = { term.in, term.out, &printer, nullptr };
    line_editor* editor = line_editor_create(desc);
    editor->set_generator(lua_generator);

    str<> out;
    while (editor->edit(out))
        if (out.equals("exit"))
            break;

    line_editor_destroy(editor);

    shutdown_recognizer();
    shutdown_task_manager(true/*final*/);

    return 0;
}

//------------------------------------------------------------------------------
static int32 hookline(app_context::desc& app_desc)
{
    // Get function in host exe.
    FARPROC worker = GetProcAddress(nullptr, "testbed_hook_loop");
    if (!worker)
    {
        fputs("Unable to find exported testbed function in exe.", stderr);
        return 1;
    }

    SetEnvironmentVariableW(L"prompt", L"clink $ ");

    // Simulate injection.
    delete app_context::get();
    if (!initialise_clink(app_desc))
        return 1;

    // Call function in host exe.
    worker();

    return 0;
}

//------------------------------------------------------------------------------
int32 testbed(int32 argc, char** argv)
{
    static const char* help_usage = "Usage: testbed [options]\n";

    static const struct option options[] = {
        { "hook",        no_argument,        nullptr, 'd' },
        { "scripts",     required_argument,  nullptr, 's' },
        { "profile",     required_argument,  nullptr, 'p' },
        { "help",        no_argument,        nullptr, 'h' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-d, --hook",           "Hook and use ReadConsoleW.",
        "-s, --scripts <path>", "Alternative path to load .lua scripts from.",
        "-p, --profile <path>", "Specifies an alternative path for profile data.",
        "-h, --help",           "Shows this help text.",
        nullptr
    };

    // Parse arguments
    bool hook = false;
    app_context::desc app_desc;
    int32 i;
    int32 ret = 1;
    while ((i = getopt_long(argc, argv, "?hp:s:d", options, nullptr)) != -1)
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

        case 'd':
            hook = true;
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
            puts("By default this starts a loop of calling editor->edit() directly.  Run\n"
                 "'clink testbed --hook' to hook the ReadConsoleW API and start a loop of\n"
                 "calling that, as though it were injected in cmd.exe (editing works, but\n"
                 "of course nothing happens with the input since the host isn't cmd.exe).");
            return ret;
        }
    }

    if (hook)
    {
        app_desc.id = GetCurrentProcessId();
        app_desc.force = true; // Skip the usual cmd.exe check.
        ret = hookline(app_desc);
    }
    else
    {
        ret = editline();
    }

    shutdown_recognizer();

    return ret;
}
