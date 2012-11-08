/* Copyright (c) 2012 Martin Ridgers
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
#include "shared/settings.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
static settings_t*  g_settings      = NULL;

//------------------------------------------------------------------------------
static const setting_decl_t g_clink_settings_decl[] = {
    {
        "ctrld_exits",
        "Ctrl-D exits",
        "Ctrl-D exits the process when it is pressed on an empty line.",
        SETTING_TYPE_BOOL,
        0, "1"
    },
    {
        "passthrough_ctrlc",
        "Ctrl-C raises exception",
        "When Ctrl-C is pressed Clink will pass it thourgh to the parent "
        "by raising the appropriate exception.",
        SETTING_TYPE_BOOL,
        0, "1"
    },
    {
        "esc_clears_line",
        "Esc clears line",
        "Clink clears the current line when Esc is pressed (unless Readline's "
        "Vi mode is enabled).",
        SETTING_TYPE_BOOL,
        0, "1"
    },
    {
        "match_colour",
        "Match display colour",
        "Colour to use when displaying matches. A value less than 0 will be "
        "the opposite of the default colour.",
        SETTING_TYPE_INT,
        0, "-1"
    },
};

//------------------------------------------------------------------------------
static void get_settings_file(char* buffer, int buffer_size)
{
    get_config_dir(buffer, buffer_size);
    str_cat(buffer, "/settings", buffer_size);
}

//------------------------------------------------------------------------------
void initialise_clink_settings(lua_State* lua)
{
    char settings_file[MAX_PATH];

    get_settings_file(settings_file, sizeof_array(settings_file));

    g_settings = settings_init(
        g_clink_settings_decl,
        sizeof_array(g_clink_settings_decl)
    );

    settings_load(g_settings, settings_file);
    settings_save(g_settings, settings_file);   // Force a settings file to disk
}

//------------------------------------------------------------------------------
void shutdown_clink_settings()
{
    char settings_file[MAX_PATH];

    if (g_settings == NULL)
    {
        return;
    }

    get_settings_file(settings_file, sizeof_array(settings_file));

    settings_shutdown(g_settings);

    g_settings = NULL;
}

//------------------------------------------------------------------------------
int get_clink_setting_int(const char* name)
{
    if (g_settings == NULL)
    {
        return 0;
    }
    
    return settings_get_int(g_settings, name);
}

//------------------------------------------------------------------------------
const char* get_clink_setting_str(const char* name)
{
    if (g_settings == NULL)
    {
        return "";
    }
    
    return settings_get_str(g_settings, name);
}

// vim: expandtab
