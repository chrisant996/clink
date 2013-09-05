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
const setting_decl_t* get_clink_settings_decl(int*);
const setting_decl_t* get_decl(settings_t*, const char*);
void                  wrapped_write(FILE*, const char*, const char*, int);

static settings_t*    g_settings;

//------------------------------------------------------------------------------
static int print_keys()
{
    int i;
    int count;
    const setting_decl_t* decl;

    decl = get_clink_settings_decl(&count);
    if (decl == NULL)
    {
        return 1;
    }

    puts("Available options:");
    for (i = 0; i < count; ++i)
    {
        printf("%16d %4s %s\n",
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
    const setting_decl_t* decl = get_decl(g_settings, key);
    if (decl == NULL)
    {
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
    const setting_decl_t* decl = get_decl(g_settings, key);
    if (decl == NULL)
    {
        return 1;
    }

    settings_set(g_settings, key, value);

    puts("...set");
    return 0;
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

    // Load Clink's settings.
    g_settings = initialise_clink_settings();
    if (g_settings == NULL)
    {
        puts("ERROR: Failed to load Clink's settings.");
        return 1;
    }

    // List or set Clink's settings.
    ret = 0;
    switch (argc)
    {
    case 0:
    case 1:     ret = print_keys();                 break;
    case 2:     ret = print_value(argv[1]);         break;
    default:    ret = set_value(argv[1], argv[2]);
                if (ret)
                {
                    /* SAVE!!! */
                }
                break;
    }

    settings_shutdown(g_settings);
    return ret;
}

// vim: expandtab
