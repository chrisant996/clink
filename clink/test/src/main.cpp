// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

//------------------------------------------------------------------------------
extern bool g_force_load_debugger;

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    argc--, argv++;

    bool timer = false;

    while (argc > 0)
    {
        if (!strcmp(argv[0], "-?") || !strcmp(argv[0], "--help"))
        {
            puts("Options:\n"
                 "  -?        Show this help.\n"
                 "  -d        Load Lua debugger.\n"
                 "  -t        Show execution time.");
            return 1;
        }
        else if (!strcmp(argv[0], "-d"))
        {
            g_force_load_debugger = true;
        }
        else if (!strcmp(argv[0], "-t"))
        {
            timer = true;
        }
        else if (!strcmp(argv[0], "--"))
        {
        }
        else
        {
            break;
        }

        argc--, argv++;
    }

    DWORD start = GetTickCount();

    const char* prefix = (argc > 0) ? argv[0] : "";
    int result = (clatch::run(prefix) != true);

    if (timer)
    {
        DWORD elapsed = GetTickCount() - start;
        printf("\nElapsed time %u.%03u seconds.\n", elapsed / 1000, elapsed % 1000);
    }

    return result;
}
