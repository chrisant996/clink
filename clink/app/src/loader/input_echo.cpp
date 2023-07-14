// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <core/settings.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_helpers.h>
#include <utils/app_context.h>

//------------------------------------------------------------------------------
extern setting_bool g_altf4_exits;

//------------------------------------------------------------------------------
int32 input_echo(int32 argc, char** argv)
{
    bool verbose_input = false;
    bool mouse_input = false;

    for (int32 i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (_stricmp(arg, "--help") == 0 || _stricmp(arg, "-h") == 0 || _stricmp(arg, "-?") == 0)
        {
            extern void puts_clink_header();
            puts_clink_header();
            printf("Usage: %s\n\n", argv[0]);
            puts("Echos the sequence of characters for each key pressed.\n");
            return 0;
        }
        if (_stricmp(arg, "--verbose") == 0 || _stricmp(arg, "-v") == 0)
            verbose_input = true;
        else if (_stricmp(arg, "--mouse") == 0 || _stricmp(arg, "-m") == 0)
            mouse_input = true; // This is only useful for troubleshooting terminal issues.
    }

    set_verbose_input(verbose_input);

    puts("Type a key to see its key sequence string.\nPress Ctrl+C when finished.");

    // Load the settings from disk, since terminal input is affected by settings.
    str<280> settings_file;
    str<280> default_settings_file;
    app_context::get()->get_settings_path(settings_file);
    app_context::get()->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());

    g_altf4_exits.set("0"); // Override the setting so Alt-F4 can be reported.

    console_config cc(nullptr, mouse_input);

    terminal terminal = terminal_create();
    terminal_in& input = *terminal.in;
    input.begin();

    bool quit = false;
    while (!quit)
    {
        input.select();

        bool need_quote = true;
        while (1)
        {
            int32 c = input.read();
            if (c < 0)
                break;

            if (need_quote)
            {
                need_quote = false;
                printf("\"");
            }

            if (c > 0x7f)
                printf("\\x%02x", unsigned(c));
            else if (c == 0x1b)
                printf("\\e");
            else if (c == 0x7f)
                printf("Rubout");
            else if (c < 0x20)
                printf("\\C-%c", c|0x40);
            else
                printf("%c", c);

            if (quit = (c == ('C' & 0x1f))) // Ctrl-c
                break;
        }

        if (!need_quote)
            printf("\"");

        puts("");
    }

    input.end();
    return 0;
}
