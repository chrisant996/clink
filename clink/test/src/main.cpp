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
void set_noasync_recognizer();
void set_test_harness();
extern bool g_force_load_debugger;
extern bool g_force_break_on_error;

//------------------------------------------------------------------------------
// NOTE:  If you get a linker error about these being "already defined", then
// probably a new global function has been added in app/src/ that needs to be
// stubbed out here.
#ifdef DEBUG
bool g_suppress_signal_assert = false;
#endif
void host_cmd_enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line) { assert(false); }
void host_cleanup_after_signal() {}
void host_set_last_prompt(const char* prompt, uint32 length) { assert(false); }

//------------------------------------------------------------------------------
int32 main(int32 argc, char** argv)
{
    argc--, argv++;

    bool timer = false;

#ifdef DEBUG
    settings::TEST_set_ever_loaded();
#endif

    os::set_shellname(L"clink_test_harness");
    set_noasync_recognizer();
    set_test_harness();

    _rl_bell_preference = VISIBLE_BELL;     // Because audible is annoying.

    int32 d_flag = 0;

    while (argc > 0)
    {
        if (!strcmp(argv[0], "-?") || !strcmp(argv[0], "--help"))
        {
            puts("Options:\n"
                 "  -?        Show this help.\n"
                 "  -d        Load Lua debugger.\n"
                 "  -dd       Force break on Lua errors.\n"
                 "  -t        Show execution time.");
            return 1;
        }
        else if (!strcmp(argv[0], "-d"))
        {
            d_flag++;
            g_force_load_debugger = true;
            g_force_break_on_error = (d_flag > 1);
        }
        else if (!strcmp(argv[0], "-dd"))
        {
            d_flag = 2;
            g_force_break_on_error = true;
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
    shutdown_task_manager(true/*final*/);

    if (timer)
    {
        DWORD elapsed = GetTickCount() - start;
        printf("\nElapsed time %u.%03u seconds.\n", elapsed / 1000, elapsed % 1000);
    }

    return result;
}
