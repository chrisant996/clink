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
        "Pressing Ctrl-D exits session",
        "Ctrl-D exits cmd.exe when it is pressed on an empty line.",
        SETTING_TYPE_BOOL,
        0, "1"
    },
    {
        "esc_clears_line",
        "Toggle if pressing Esc clears line",
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
        "Colour of the prompt",
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
        "history_file_lines",
        "Lines of history saved to disk",
        "When set to a positive integer this is the number of lines of history "
        "that will persist when Clink saves the command history to disk. Use 0 "
        "for infinite lines and <0 to disable history persistence.",
        SETTING_TYPE_INT,
        0, "10000"
    },
    {
        "history_ignore_space",
        "Skip adding lines prefixed with whitespace",
        "Ignore lines that begin with whitespace when adding lines in to "
        "the history.",
        SETTING_TYPE_BOOL,
        0, "0"
    },
    {
        "history_dupe_mode",
        "Controls how duplicate entries are handled",
        "If a line is a duplicate of an existing history entry Clink will "
        "erase the duplicate when this is set 2. A value of 1 will not add "
        "duplicates to the history and a value of 0 will always add lines. "
        "Note that history is not deduplicated when reading/writing to disk.",
        SETTING_TYPE_ENUM,
        "Always add\0Ignore\0Erase previous",
        "2"
    },
    {
        "history_io",
        "Read/write history file each line edited",
        "When non-zero the history will be read from disk before editing a "
        "new line and written to disk afterwards.",
        SETTING_TYPE_BOOL,
        0, "0"
    },
    {
        "history_expand_mode",
        "Sets how command history expansion is applied",
        "The '!' character in an entered line can be interpreted to introduce "
        "words from the history. This can be enabled and disable by setting this "
        "value to 1 or 0. Values or 2, 3 or 4 will skip any ! character quoted "
        "in single, double, or both quotes respectively.",
        SETTING_TYPE_ENUM,
        "Off\0On\0Not in single quotes\0Not in double quote\0Not in any quotes",
        "4"
    },
    {
        "use_altgr_substitute",
        "Support Windows' Ctrl-Alt substitute for AltGr",
        "Windows provides Ctrl-Alt as a substitute for AltGr, historically to "
        "support keyboards with no AltGr key. This may collide with some of "
        "Readline's bindings.",
        SETTING_TYPE_BOOL,
        0, "1"
    },
    {
        "strip_crlf_on_paste",
        "Strips CR and LF chars on paste",
        "Setting this to a value >0 will make Clink strip CR and LF characters "
        "from text pasted into the current line. Set this to 1 to strip all "
        "newline characters and 2 to replace them with a space.",
        SETTING_TYPE_ENUM,
        "Paste unchanged\0Strip\0As space",
        "2"
    },
    {
        "ansi_code_support",
        "Enables basic ANSI escape code support",
        "When printing the prompt, Clink has basic built-in support for SGR "
        "ANSI escape codes to control the text colours. This is automatically "
        "disabled if a third party tool is detected that also provides this "
        "facility. It can also be disabled by setting this to 0.",
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
