// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host/host_lua.h"
#include "utils/app_context.h"
#include "utils/usage.h"

#include <core/base.h>
#include <core/path.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <lua/lua_script_loader.h>
#include <lua/prompt.h>

#include <getopt.h>

//------------------------------------------------------------------------------
extern void host_load_app_scripts(lua_state& lua);
extern setting_str g_customprompt;

//------------------------------------------------------------------------------
static void list_keys()
{
    for (auto iter = settings::first(); auto* next = iter.next();)
        puts(next->get_name());
}

//------------------------------------------------------------------------------
static void list_options(lua_state& lua, const char* key)
{
    const setting* setting = settings::find(key);
    if (setting == nullptr)
        return;

    if (stricmp(key, "autosuggest.strategy") == 0)
    {
        lua_State *state = lua.get_state();
        save_stack_top ss(state);
        lua.push_named_function(state, "clink._print_suggesters");
        lua.pcall_silent(state, 0, 0);
        return;
    }

    switch (setting->get_type())
    {
    case setting::type_int:
    case setting::type_string:
        break;

    case setting::type_bool:
        puts("true");
        puts("false");
        break;

    case setting::type_enum:
        {
            const char* options = ((const setting_enum*)setting)->get_options();
            str_tokeniser tokens(options, ",");
            const char* start;
            int32 length;
            while (tokens.next(start, length))
                printf("%.*s\n", length, start);
        }
        break;

    case setting::type_color:
        {
            static const char* const color_keywords[] =
            {
                "bold", "nobold", "underline",
                "reverse", "italic",
                "bright", "default", "normal", "on",
                "black", "red", "green", "yellow",
                "blue", "cyan", "magenta", "white",
                "sgr",
            };

            for (auto keyword : color_keywords)
                puts(keyword);
        }
        break;
    }

    puts("clear");
}

//------------------------------------------------------------------------------
static bool print_value(bool describe, const char* key, bool compat, bool list=false)
{
    const setting* setting = settings::find(key);
    if (setting == nullptr)
    {
        std::vector<settings::setting_name_value> migrated_settings;
        if (migrate_setting(key, nullptr, migrated_settings))
        {
            bool ret = true;
            bool printed = false;
            for (const auto& pair : migrated_settings)
            {
                if (printed)
                    puts("");
                else
                    printed = true;
                ret = print_value(describe, pair.name.c_str(), compat) && ret;
            }
            return ret;
        }

        printf("ERROR: Setting '%s' not found.\n", key);
        return false;
    }

    if (list && g_printer)
    {
        str<> s;
        static const char bold[] = "\x1b[1m";
        static const char norm[] = "\x1b[m";
        s.format("        %sName: %s%s\n", bold, setting->get_name(), norm);
        g_printer->print(s.c_str(), s.length());
    }
    else
    {
        printf("        Name: %s\n", setting->get_name());
    }

    printf(" Description: %s\n", setting->get_short_desc());

    // Output an enum-type setting's options.
    if (setting->get_type() == setting::type_enum)
        printf("     Options: %s\n", ((setting_enum*)setting)->get_options());
    else if (setting->get_type() == setting::type_color)
        printf("      Syntax: [bold italic underline reverse] [bright] color on [bright] color\n");

    str<> value;
    setting->get_descriptive(value, compat);
    printf("       Value: %s\n", value.c_str());

    setting->get_default(value);
    if (setting->get_type() == setting::type_color)
    {
        str<> tmp;
        settings::parse_color(value.c_str(), tmp);
        settings::format_color(tmp.c_str(), value, false);
    }
    printf("     Default: %s\n", value.c_str());

    const char* long_desc = setting->get_long_desc();
    if (long_desc != nullptr && *long_desc)
        printf("\n%s\n", setting->get_long_desc());

    if (setting->get_type() == setting::type_color)
        printf("\nVisit https://chrisant996.github.io/clink/clink.html#color-settings\nfor details on setting colors.\n");

    return true;
}

//------------------------------------------------------------------------------
static bool print_keys(bool describe, bool details, bool compat, const char* prefix=nullptr)
{
    size_t prefix_len = prefix ? strlen(prefix) : 0;

    int32 longest = 0;
    if (!details)
    {
        for (auto iter = settings::first(); auto* next = iter.next();)
        {
            if (!prefix || !_strnicmp(next->get_name(), prefix, prefix_len))
                longest = max(longest, int32(strlen(next->get_name())));
        }
    }

    str<> value;
    bool printed = false;
    for (auto iter = settings::first(); auto* next = iter.next();)
    {
        if (!prefix || !_strnicmp(next->get_name(), prefix, prefix_len))
        {
            const char* name = next->get_name();
            if (details)
            {
                if (printed)
                    puts("");
                print_value(describe, name, true);
                printed = true;
            }
            else
            {
                const char* col2;
                if (describe)
                {
                    col2 = next->get_short_desc();
                }
                else
                {
                    next->get_descriptive(value, compat);
                    col2 = value.c_str();
                }
                printf("%-*s  %s\n", longest, name, col2);
            }
        }
    }

    return true;
}

//------------------------------------------------------------------------------
static bool set_value_impl(const char* key, const char* value, bool compat)
{
    setting* setting = settings::find(key);
    if (setting == nullptr)
    {
        std::vector<settings::setting_name_value> migrated_settings;
        if (migrate_setting(key, value, migrated_settings))
        {
            bool ret = true;
            for (const auto& pair : migrated_settings)
                ret = set_value_impl(pair.name.c_str(), pair.value.c_str(), compat) && ret;
            return ret;
        }

        printf("ERROR: Setting '%s' not found.\n", key);
        return false;
    }

    if (!value)
    {
        setting->set();
    }
    else
    {
        if (!setting->set(value))
        {
            printf("ERROR: Failed to set value '%s'.\n", key);
            return false;
        }
    }

    str<> result;
    setting->get_descriptive(result, compat);
    printf("Setting '%s' %sset to '%s'\n", key, value ? "" : "re", result.c_str());
    return true;
}

//------------------------------------------------------------------------------
static bool set_value(const char* key, bool compat, char** argv=nullptr, int32 argc=0)
{
    if (!argc)
        return set_value_impl(key, nullptr, compat);

    str<> value;
    for (int32 c = argc; c--;)
    {
        if (value.length())
            value << " ";
        value << *argv;
        argv++;
    }

    return set_value_impl(key, value.c_str(), compat);
}

//------------------------------------------------------------------------------
static void print_help()
{
    static const char* const help[] = {
        "setting_name",     "Name of the setting whose value is to be set.",
        "value",            "Value to set the setting to.",
        "-d, --describe",   "Show descriptions of settings (instead of values).",
        "-i, --info",       "Show detailed info for each setting when '*' is used.",
        "-h, --help",       "Shows this help text.",
        "-C, --compat",     "Print backward-compatible color setting values.",
        nullptr
    };

    puts_clink_header();
    puts("Usage: set [options] [<setting_name> [clear|<value>]]\n");

    puts_help(help);

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
}

//------------------------------------------------------------------------------
static bool save_settings(const char* settings_file)
{
    if (settings::save(settings_file))
        return true;

    printf("ERROR: Unable to write to settings file '%s'.\n", settings_file);
    return false;
}

//------------------------------------------------------------------------------
int32 set(int32 argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "help", no_argument, nullptr, 'h' },
        { "list", no_argument, nullptr, 'l' },
        { "describe", no_argument, nullptr, 'd' },
        { "info", no_argument, nullptr, 'i' },
        { "compat", no_argument, nullptr, 'C' },
        {}
    };

    bool complete = false;
    bool describe = false;
    bool details = false;
    bool compat = false;
    int32 i;
    while ((i = getopt_long(argc, argv, "+?hldiC", options, nullptr)) != -1)
    {
        switch (i)
        {
        default:
        case '?':
        case 'h': print_help();     return 0;
        case 'l': complete = true;  break;
        case 'd': describe = true;  break;
        case 'i': details = true;   break;
        case 'C': compat = true;    break;
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
    lua.load_scripts();

    // Load the clink.customprompt module so its settings are available.
    str_moveable customprompt;
    if (!os::get_env("CLINK_CUSTOMPROMPT", customprompt))
        g_customprompt.get(customprompt);
    lua.activate_clinkprompt_module(customprompt.c_str());

    // List or set Clink's settings.
    if (complete)
    {
        (optind < argc) ? list_options(lua, argv[0]) : list_keys();
        return 0;
    }

    DWORD dummy;
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dummy))
        g_printer = nullptr;

    switch (argc)
    {
    case 0:
        return (print_keys(describe, false, compat) != true);

    case 1:
        if (argv[0][0] && argv[0][strlen(argv[0]) - 1] == '*')
        {
            str<> prefix(argv[0]);
            prefix.truncate(prefix.length() - 1);
            return (print_keys(describe, details, compat, prefix.c_str()) != true);
        }
        return (print_value(describe, argv[0], compat) != true);

    default:
        if (_stricmp(argv[1], "clear") == 0)
        {
            if (set_value(argv[0], compat))
                return (save_settings(settings_file.c_str()) != true);
        }
        else if (set_value(argv[0], compat, argv + 1, argc - 1))
            return (save_settings(settings_file.c_str()) != true);
    }

    return 1;
}
