// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host/host_lua.h"
#include "utils/app_context.h"
#include "utils/usage.h"

#include <core/base.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <lib/match_colors.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <lua/lua_script_loader.h>
#include <lua/prompt.h>

#include <getopt.h>

extern "C" {
#define lua_c
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
};

//------------------------------------------------------------------------------
extern void host_load_app_scripts(lua_state& lua);

//------------------------------------------------------------------------------
static bool do_config(lua_state& lua, int32 argc, char** argv)
{
    lua_State* state = lua.get_state();

    lua.push_named_function(state, "config_loader.do_config");

    lua_createtable(state, 10, 0);
    for (int32 i = 0; i < argc; ++i)
    {
        lua_pushstring(state, argv[i]);
        lua_rawseti(state, -2, i + 1);
    }

    lua.pcall_silent(state, 1, 1);
    return lua_toboolean(state, -1);
}

//------------------------------------------------------------------------------
static void print_help()
{
    static const char* const help[] = {
        "list",             "List color themes",
        "load",             "Load a color theme",
        "save",             "Save the current color theme",
        "show",             "Show what the theme looks like",
        "print",            "Print a color theme",
        "-h, --help",       "Shows this help text.",
        nullptr
    };

    puts_clink_header();
    puts("Usage: config [commands]\n");

    puts_help(help);

#if 0
    puts("If 'setting_name' is omitted then all settings are listed.  Omit 'value'\n"
        "for more detailed info about a setting and use a value of 'clear' to reset\n"
        "the setting to its default value.\n"
        "\n"
        "If 'setting_name' ends with '*' then it is a prefix, and all settings\n"
        "matching the prefix are listed.  The --info flag includes detailed info\n"
        "for each listed setting.\n"
        "\n"
        "The --compat flag selects backward-compatible mode when printing color setting\n"
        "values.  This is only needed when the output from the command will be used as\n"
        "input to an older version that doesn't support newer color syntax.");
#endif
}

//------------------------------------------------------------------------------
int32 config(int32 argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "help", no_argument, nullptr, 'h' },
        {}
    };

    int32 i;
    while ((i = getopt_long(argc, argv, "?h", options, nullptr)) != -1)
    {
        switch (i)
        {
        default:
        case '?':
        case 'h': print_help();     return 0;
        }
    }

    argc -= optind;
    argv += optind;

    terminal term = terminal_create();
    printer printer(*term.out);
    printer_context printer_context(term.out, &printer);

    console_config cc(nullptr, false/*accept_mouse_input*/);

    // Load the settings from disk.
    str_moveable settings_file;
    str_moveable default_settings_file;
    app_context::get()->get_settings_path(settings_file);
    app_context::get()->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());

    // Load all lua state too as there is settings declared in scripts.  The
    // load function handles deferred load for settings declared in scripts.
    host_lua lua;
    prompt_filter prompt_filter(lua);
    host_load_app_scripts(lua);
    lua_load_script(lua, app, loader_config);
    lua.load_scripts();

    parse_match_colors();

    DWORD dummy;
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dummy))
        g_printer = nullptr;

    return do_config(lua, argc, argv) ? 0 : 1;
}
