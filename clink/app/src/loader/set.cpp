// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"

#include <core/base.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>

//------------------------------------------------------------------------------
void puts_help(const char**, int);

//------------------------------------------------------------------------------
static bool print_keys()
{
    for (setting* next = settings::first(); next != nullptr; next = next->next())
    {
        printf("# %s\n", next->get_short_desc());

        str<> value; next->get(value);
        const char* name = next->get_name();
        printf("%s = %s\n\n", name, value.c_str());
    }

    return true;
}

//------------------------------------------------------------------------------
static bool print_value(const char* key)
{
    const setting* setting = settings::find(key);
    if (setting == nullptr)
    {
        printf("ERROR: Setting '%s' not found.\n", key);
        return false;
    }

    printf("        Name: %s\n", setting->get_name());
    printf(" Description: %s\n", setting->get_short_desc());

    // Output an enum-type setting's options.
    if (setting->get_type() == setting::type_enum)
        printf("     Options: %s\n", ((setting_enum*)setting)->get_options());

    str<> value;
    setting->get(value);
    printf("       Value: %s\n", value.c_str());

    const char* long_desc = setting->get_long_desc();
    if (long_desc != nullptr && *long_desc)
        printf("\n%s\n", setting->get_long_desc());

    return true;
}

//------------------------------------------------------------------------------
static bool set_value(const char* key, const char* value)
{
    setting* setting = settings::find(key);
    if (setting == nullptr)
    {
        printf("ERROR: Setting '%s' not found.\n", key);
        return false;
    }

    if (!setting->set(value))
    {
        printf("ERROR: Failed to set value for '%s'.\n", key);
        return false;
    }

    str<> result;
    setting->get(result);
    printf("Settings '%s' set to '%s'\n", key, result.c_str());
    return true;
}

//------------------------------------------------------------------------------
static void print_help()
{
    extern const char* g_clink_header;

    const char* help[] = {
        "setting_name", "Name of the setting who's value is to be set.",
        "value",        "Value to set the setting to."
    };

    puts(g_clink_header);
    puts("Usage: set [setting_name] [value]\n");

    puts_help(help, sizeof_array(help));

    puts("If 'settings_name' is omitted then all settings are list. Omit 'value' for\n"
        "more detailed info about a setting.\n");
}

//------------------------------------------------------------------------------
int set(int argc, char** argv)
{
    bool ret = true;

    // Load the settings from disk.
    str<280> settings_file;
    app_context::get()->get_settings_path(settings_file);
    settings::load(settings_file.c_str());

    // List or set Clink's settings.
    ret = false;
    switch (argc)
    {
    case 0:
    case 1:
        ret = print_keys();
        break;

    case 2:
        if (_stricmp(argv[1], "--help") == 0 || _stricmp(argv[1], "-h") == 0)
            print_help();
        else
            ret = print_value(argv[1]);

        break;

    default:
        ret = set_value(argv[1], argv[2]);
        if (ret)
        {
            str<280> settings_file;
            app_context::get()->get_settings_path(settings_file);
            settings::save(settings_file.c_str());
        }
        break;
    }

    return (ret == true);
}
