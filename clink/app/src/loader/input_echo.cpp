// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <core/settings.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/config.h>
#include <utils/app_context.h>

//------------------------------------------------------------------------------
int input_echo(int argc, char** argv)
{
    bool verbose_input = false;

    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (_stricmp(arg, "--help") == 0 || _stricmp(arg, "-h") == 0)
        {
            extern const char* g_clink_header;
            puts(g_clink_header);
            printf("Usage: %s\n\n", argv[0]);
            puts("Echos the sequence of characters for each key pressed.\n");
            return 0;
        }
        if (_stricmp(arg, "--verbose") == 0 || _stricmp(arg, "-v") == 0)
            verbose_input = true;
    }

    extern void set_verbose_input(bool verbose);
    set_verbose_input(verbose_input);

    // Load the settings from disk, since terminal input is affected by settings.
    str<280> settings_file;
    app_context::get()->get_settings_path(settings_file);
    settings::load(settings_file.c_str());

    console_config cc;

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
            int c = input.read();
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
