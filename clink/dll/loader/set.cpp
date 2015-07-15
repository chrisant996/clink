// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "paths.h"
#include "settings.h"

#include <core/base.h>
#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
void*                 initialise_clink_settings();
void                  puts_help(const char**, int);
static settings_t*    g_settings;
static str<MAX_PATH>  g_settings_path;

//------------------------------------------------------------------------------
static int print_keys()
{
    int i, n;
    const setting_decl_t* decl;

    decl = settings_get_decls(g_settings);
    if (decl == nullptr)
    {
        puts("ERROR: Failed to find settings decl.");
        return 0;
    }

    puts("Available options:\n");
    for (i = 0, n = settings_get_decl_count(g_settings); i < n; ++i)
    {
        static const char dots[] = ".......................... ";
        const char* name = decl->name;

        printf("%s ", name);
        int dot_count = sizeof_array(dots) - (int)strlen(name);
        if (dot_count > 0)
            printf("%s", dots + sizeof_array(dots) - dot_count);

        printf("%-6s %s\n", settings_get_str(g_settings, name), decl->friendly_name);

        ++decl;
    }

    printf("\nSettings path: %s\n", g_settings_path.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int print_value(const char* key)
{
    const setting_decl_t* decl = settings_get_decl_by_name(g_settings, key);
    if (decl == nullptr)
    {
        printf("ERROR: Setting '%s' not found.\n", key);
        return 0;
    }

    printf("         Name: %s\n", decl->name);
    printf("  Description: %s\n", decl->friendly_name);
    printf("Current value: %s\n", settings_get_str(g_settings, key));

    if (decl->type == SETTING_TYPE_ENUM)
    {
        int i = 0;
        const char* param = decl->type_param;

        printf("       Values: ");
        while (*param)
        {
            printf("%*d = %s\n", (i ? 16 : 1), i, param);
            param += strlen(param) + 1;
            ++i;
        }
    }

    puts("");
    printf("\n%s\n", decl->description);

    return 1;
}

//------------------------------------------------------------------------------
static int set_value(const char* key, const char* value)
{
    const setting_decl_t* decl = settings_get_decl_by_name(g_settings, key);
    if (decl == nullptr)
    {
        printf("ERROR: Setting '%s' not found.\n", key);
        return 0;
    }

    settings_set(g_settings, key, value);

    printf("Settings '%s' set to '%s'\n", key, settings_get_str(g_settings, key));
    return 1;
}

//------------------------------------------------------------------------------
void print_usage()
{
    extern const char* g_clink_header;

    const char* help[] = {
        "setting_name", "Name of the setting who's value is to be set.",
        "value",        "Value to set the setting to."
    };

    puts(g_clink_header);
    puts("  Usage: set [setting_name] [value]\n");
    puts_help(help, sizeof_array(help));
    puts("  If 'settings_name' is omitted then all settings are list.");
    puts("  Omit 'value' for more detailed info about a setting.\n");
}

//------------------------------------------------------------------------------
int set(int argc, char** argv)
{
    int ret;

    // Check we're running from a Clink session.
    extern int g_in_clink_context;
    if (!g_in_clink_context)
    {
        puts("ERROR: The 'set' verb must be run from a process with Clink present");
        return 1;
    }

    // Get the path where Clink's storing its settings.
    get_config_dir(g_settings_path);
    g_settings_path << "/settings";
    path::clean(g_settings_path);

    // Load Clink's settings.
    g_settings = (settings_t*)initialise_clink_settings();
    if (g_settings == nullptr)
    {
        printf("ERROR: Failed to load Clink's settings from '%s'.", g_settings_path.c_str());
        return 1;
    }

    // List or set Clink's settings.
    ret = 1;
    switch (argc)
    {
    case 0:
    case 1:
        ret = print_keys();
        break;

    case 2:
        if (_stricmp(argv[1], "--help") == 0 || _stricmp(argv[1], "-h") == 0)
        {
            print_usage();
        }
        else
            ret = print_value(argv[1]);

        break;

    default:
        if (set_value(argv[1], argv[2]))
            ret = settings_save(g_settings, g_settings_path.c_str());
        break;
    }

    settings_shutdown(g_settings);
    return !ret;
}
