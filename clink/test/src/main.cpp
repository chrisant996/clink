// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <core/settings.h>
#include <core/os.h>
#include <lib/recognizer.h>
#include <lua/lua_task_manager.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
}

#include <list>
#include <assert.h>

//------------------------------------------------------------------------------
extern bool g_force_load_debugger;

//------------------------------------------------------------------------------
#ifdef DEBUG
bool g_suppress_signal_assert = false;
#endif

//------------------------------------------------------------------------------
void host_cmd_enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line)
{
    assert(false);
}

//------------------------------------------------------------------------------
void host_cleanup_after_signal()
{
}

//------------------------------------------------------------------------------
int32 main(int32 argc, char** argv)
{
    argc--, argv++;

    bool timer = false;

#ifdef DEBUG
    settings::TEST_set_ever_loaded();
#endif

    os::set_shellname(L"clink_test_harness");

    _rl_bell_preference = VISIBLE_BELL;     // Because audible is annoying.

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

    clatch::colors::initialize();

    const char* prefix = (argc > 0) ? argv[0] : "";
    int32 result = (clatch::run(prefix) != true);

    shutdown_recognizer();
    shutdown_task_manager();

    if (timer)
    {
        DWORD elapsed = GetTickCount() - start;
        printf("\nElapsed time %u.%03u seconds.\n", elapsed / 1000, elapsed % 1000);
    }

    return result;
}
