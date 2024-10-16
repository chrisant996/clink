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
        "prompt",           "Configure the custom prompt for Clink.",
        "theme",            "Configure the color theme for Clink.",
        "-h, --help",       "Shows this help text.",
        nullptr
    };

    puts_clink_header();
    puts("Usage: config [commands]\n");

    puts("Commands:");

    puts_help(help);
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
    while ((i = getopt_long(argc, argv, "+?h", options, nullptr)) != -1)
    {
        switch (i)
        {
        default:
        case '?':
        case 'h':
do_help:
            print_help();
            return 0;
        }
    }

    argc -= optind;
    argv += optind;

    if (!argc)
        goto do_help;

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

    // Make prompt.async appear off, so that prompt filters don't think async
    // prompt filtering is available in this context (cuz it's not!).
    const char* const script =
    "local old_get = settings.get\n"
    "settings.get = function(name, descriptive)\n"
    "   if name == 'prompt.async' then return false\n"
    "   else return old_get(name, descriptive)\n"
    "   end\n"
    "end";
    static_cast<lua_state&>(lua).do_string(script, -1);

    parse_match_colors();

    DWORD dummy;
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dummy))
        g_printer = nullptr;

    return do_config(lua, argc, argv) ? 0 : 1;
}
