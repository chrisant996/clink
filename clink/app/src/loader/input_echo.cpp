// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/config.h>

//------------------------------------------------------------------------------
int input_echo(int argc, char** argv)
{
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
    }

    console_config cc;

    terminal terminal = terminal_create();
    terminal_in& input = *terminal.in;
    input.begin();

    bool quit = false;
    while (!quit)
    {
        input.select();
        while (1)
        {
            int c = input.read();
            if (c < 0)
                break;

            if (c > 0x7f)
                printf("\\x%02x", unsigned(c));
            else if (c < 0x20)
                printf("^%c", c|0x40);
            else
                printf("%c", c);

            if (quit = (c == ('C' & 0x1f))) // Ctrl-c
                break;
        }

        puts("");
    }

    input.end();
    return 0;
}
