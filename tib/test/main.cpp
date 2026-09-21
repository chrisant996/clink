// Copyright (c) 2026 Christopher Antos
// Derived from clink/test/main.cpp, portions Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "tib.h"
#include "test.h"
#include "test_util.h"

#include <list>
#include <assert.h>
#include <crtdbg.h>

#ifdef DISABLE_CRT_INVALID_PARAMETER_FAILFAST
static void __cdecl do_nothing(wchar_t const*, wchar_t const*, wchar_t const*, unsigned int, uintptr_t)
{
}

static void install_crt_invalid_parameter_handler()
{
    _set_invalid_parameter_handler(do_nothing);
}
#endif

static bool s_performance_tests = false;

bool test::include_perf_tests()
{
    return s_performance_tests;
}

int main(int argc, char** argv)
{
    --argc, ++argv;

#ifdef DISABLE_CRT_INVALID_PARAMETER_FAILFAST
    install_crt_invalid_parameter_handler();
#endif

    // Override self-insert optimization, for stable test outcomes.
    tib::g_optimize_self_insert = false;

    reset_wcwidths();

    tib::set_test_harness();
    install_test_terminal();
    tib::term_begin();

    bool list = false;
    bool times = false;

    while (argc > 0)
    {
        if (!strcmp(argv[0], "-?") || !strcmp(argv[0], "--help"))
        {
            puts(  "Options:\n"
                   "  -?                Show this help.\n"
                   "  -t                Show individual test times.\n"
                   "  --list            List test names.\n"
                   "  --performance     Include performance tests.");
            return 1;
        }
        else if (!strcmp(argv[0], "-t"))
        {
            times = true;
        }
        else if (!strcmp(argv[0], "--list"))
        {
            list = true;
        }
        else if (!strcmp(argv[0], "--performance") || !strcmp(argv[0], "--perf"))
        {
            s_performance_tests = true;
            times = true;
        }
        else if (!strcmp(argv[0], "--"))
        {
        }
        else
        {
            break;
        }

        --argc, ++argv;
    }

    if (list)
    {
        test::list();
        return 0;
    }

    DWORD start = GetTickCount();

    test::colors::initialize();

    const char* prefix = (argc > 0) ? argv[0] : "";
    int32_t result = (test::run(prefix, times) != true);

    DWORD elapsed = GetTickCount() - start;
    printf("\nElapsed time %u.%03u seconds.\n", elapsed / 1000, elapsed % 1000);

    return result;
}
