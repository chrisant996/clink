// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "core/str.h"

#include <list>
#include <assert.h>

//------------------------------------------------------------------------------
extern bool g_force_load_debugger;

//------------------------------------------------------------------------------
void host_cmd_enqueue_lines(std::list<str_moveable>& lines)
{
    assert(false);
}

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

#pragma warning(push)
#pragma warning(disable:4996)
    {
        DWORD type;
        DWORD data;
        DWORD size;
        LSTATUS status = RegGetValue(HKEY_CURRENT_USER, "Console", "ForceV2", RRF_RT_REG_DWORD, &type, &data, &size);
        if (status != ERROR_SUCCESS ||
            type != REG_DWORD ||
            size != sizeof(data) ||
            data != 0)
        {
            DWORD mode;
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (GetConsoleMode(h, &mode))
            {
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
                *clatch::colors::get_colored() = true;
            }
        }
    }
#pragma warning(pop)

    const char* prefix = (argc > 0) ? argv[0] : "";
    int result = (clatch::run(prefix) != true);

    if (timer)
    {
        DWORD elapsed = GetTickCount() - start;
        printf("\nElapsed time %u.%03u seconds.\n", elapsed / 1000, elapsed % 1000);
    }

    return result;
}
