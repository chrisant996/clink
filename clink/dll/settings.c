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
static const setting_decl_t g_settings_decl[] = {
    {
        "ctrld_exits",
        "Ctrl-D exits",
        "Ctrl-D exits cmd.exe when it is pressed on an empty line.",
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
        "the opposite brightness of the default colour.",
        SETTING_TYPE_INT,
        0, "-1"
    },
    {
        "exec_match_style",
        "Executable match style",
        "Changes how Clink will match executables when there is no path "
        "separator on the line. 0 = PATH only, 1 = PATH and CWD, 2 = PATH, "
        "CWD, and directories. In all cases both executables and directories "
        "are matched when there is a path separator present. A value of -1 "
        "will disable executable matching completely.",
        SETTING_TYPE_ENUM,
        "PATH only\0PATH and CWD\0PATH, CWD, and directories",
        "2"
    },
    {
        "space_prefix_match_files",
        "Whitespace prefix matches files",
        "If the line begins with whitespace then Clink bypasses executable "
        "matching and will match all files and directories instead.",
        SETTING_TYPE_BOOL,
        0, "1"
    },
    {
        "prompt_colour",
        "Prompt colour",
        "Surrounds the prompt in ANSI escape codes to set the prompt's colour. "
        "Disabled when the value is less than 0.",
        SETTING_TYPE_INT,
        0, "-1"
    },
    {
        "terminate_autoanswer",
        "Auto-answer terminate prompt",
        "Automatically answers cmd.exe's 'Terminate batch job (Y/N)?' prompts. "
        "0 = disabled, 1 = answer 'Y', 2 = answer 'N'.",
        SETTING_TYPE_ENUM,
        "Disabled\0Answer 'Y'\0Answer 'N'",
        "0"
    },
    {
        "persist_history",
        "Persistent command history",
        "Enable or disable the saving and loading of command history "
        "between sessions.",
        SETTING_TYPE_BOOL,
        0, "1"
    },
    {
        "use_altgr_substitute",
        "Support Windows' Ctrl-Alt substitute",
        "Windows provides Ctrl-Alt as a substitute for AltGr, historically to "
        "support keyboards with no AltGr key. This may collide with some of "
        "Readline's bindings.",
        SETTING_TYPE_BOOL,
        0, "1"
    },
};

//------------------------------------------------------------------------------
static void get_settings_file(char* buffer, int buffer_size)
{
    get_config_dir(buffer, buffer_size);
    str_cat(buffer, "/settings", buffer_size);
}

//------------------------------------------------------------------------------
void* initialise_clink_settings()
{
    char settings_file[MAX_PATH];

    get_settings_file(settings_file, sizeof_array(settings_file));

    g_settings = settings_init(g_settings_decl, sizeof_array(g_settings_decl));

    if (!settings_load(g_settings, settings_file))
    {
        settings_save(g_settings, settings_file);
    }

    return g_settings;
}

//------------------------------------------------------------------------------
void shutdown_clink_settings()
{
    if (g_settings != NULL)
    {
        settings_shutdown(g_settings);
        g_settings = NULL;
    }
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

//vim: expandtab
