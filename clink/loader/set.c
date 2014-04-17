/* Copyright (c) 2013 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "shared/util.h"
#include "shared/settings.h"

//------------------------------------------------------------------------------
void*                 initialise_clink_settings();
void                  wrapped_write(FILE*, const char*, const char*, int);

static settings_t*    g_settings;
static char           g_settings_path[512];

//------------------------------------------------------------------------------
static int print_keys()
{
    int i, n;
    const setting_decl_t* decl;

    decl = settings_get_decls(g_settings);
    if (decl == NULL)
    {
        puts("ERROR: Failed to find settings decl.");
        return 1;
    }

    printf("    Settings path: %s\n", g_settings_path);
    puts("Available options:");
    for (i = 0, n = settings_get_decl_count(g_settings); i < n; ++i)
    {
        printf("%24s %4s %s\n",
            decl->name,
            settings_get_str(g_settings, decl->name),
            decl->friendly_name
        );

        ++decl;
    }

    return 0;
}

//------------------------------------------------------------------------------
static int print_value(const char* key)
{
    const setting_decl_t* decl = settings_get_decl_by_name(g_settings, key);
    if (decl == NULL)
    {
        printf("ERROR: Setting '%s' not found.\n", key);
        return 1;
    }

    printf("         Name: %s\n", decl->name);
    printf("Friendly name: %s\n", decl->friendly_name);
    printf("Current value: %s\n", settings_get_str(g_settings, key));
    puts("");
    wrapped_write(stdout, "", decl->description, 78);

    return 0;
}

//------------------------------------------------------------------------------
static int set_value(const char* key, const char* value)
{
    const setting_decl_t* decl = settings_get_decl_by_name(g_settings, key);
    if (decl == NULL)
    {
        printf("ERROR: Setting '%s' not found.\n", key);
        return 1;
    }

    settings_set(g_settings, key, value);

    printf("Settings '%s' set to '%s'\n", key, settings_get_str(g_settings, key));
    return 0;
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
    get_config_dir(g_settings_path, sizeof_array(g_settings_path));
    str_cat(g_settings_path, "/settings", sizeof_array(g_settings_path));

    // Load Clink's settings.
    g_settings = initialise_clink_settings();
    if (g_settings == NULL)
    {
        printf("ERROR: Failed to load Clink's settings from '%s'.", g_settings_path);
        return 1;
    }

    // List or set Clink's settings.
    ret = 0;
    switch (argc)
    {
    case 0:
    case 1:
        ret = print_keys();
        break;

    case 2:
        if (_stricmp(argv[1], "--help") == 0 ||
            _stricmp(argv[1], "-h") == 0)
        {
            ret = 1;
            print_usage();
        }
        else
        {
            ret = print_value(argv[1]);
        }
        break;

    default:
        ret = set_value(argv[1], argv[2]);
        if (!ret)
        {
            settings_save(g_settings, g_settings_path);
        }
        break;
    }

    settings_shutdown(g_settings);
    return ret;
}

// vim: expandtab
