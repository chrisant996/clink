// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "utils/seh_scope.h"

#include <core/base.h>
#include <core/str.h>

extern "C" {
#include <getopt.h>
}

//------------------------------------------------------------------------------
int g_in_clink_context;
int inject(int, char**);
int autorun(int, char**);
int set(int, char**);
int testbed(int, char**);

//------------------------------------------------------------------------------
void puts_help(const char** help_pairs, int count)
{
    count &= ~1;

    int max_len = -1;
    for (int i = 0, n = count; i < n; i += 2)
        max_len = max((int)strlen(help_pairs[i]), max_len);

    for (int i = 0, n = count; i < n; i += 2)
    {
        const char* arg = help_pairs[i];
        const char* desc = help_pairs[i + 1];
        printf("  %-*s  %s\n", max_len, arg, desc);
    }

    puts("");
}

//------------------------------------------------------------------------------
static void show_usage()
{
    const char* help_usage = "Usage: <verb> <verb_options>\n";
    const char* help_verbs[] = {
        "Verbs:",   "",
        "inject",   "Injects Clink into a process.",
        "autorun",  "Manage Clink's entry in cmd.exe's autorun.",
        "set",      "Adjust Clink's settings.",
        "",         "('<verb> --help' for more details).",
    };

    extern const char* g_clink_header;

    puts(g_clink_header);
    puts(help_usage);
    puts_help(help_verbs, sizeof_array(help_verbs));
}

//------------------------------------------------------------------------------
static int dispatch_verb(const char* verb, int argc, char** argv)
{
    struct {
        const char* verb;
        int (*handler)(int, char**);
    } handlers[] = {
        "inject", inject,
        "autorun", autorun,
        "set", set,
        "testbed", testbed,
    };

    for (int i = 0; i < sizeof_array(handlers); ++i)
    {
        if (strcmp(verb, handlers[i].verb) == 0)
        {
            int ret;
            int t;

            t = optind;
            optind = 1;

            ret = handlers[i].handler(argc, argv);

            optind = t;
            return ret;
        }
    }

    printf("*** ERROR: Unknown verb -- '%s'\n", verb);
    show_usage();
    return 0;
}

//------------------------------------------------------------------------------
int loader(int argc, char** argv)
{
    seh_scope seh;

    struct option options[] = {
        { "help",   no_argument,       nullptr, 'h' },
        { "cfgdir", required_argument, nullptr, 'c' },
        { nullptr,  0,                 nullptr, 0 }
    };

    // Without arguments, show help.
    if (argc <= 1)
    {
        show_usage();
        return 0;
    }

    app_context::desc app_desc;

    // Parse arguments
    int arg;
    while ((arg = getopt_long(argc, argv, "+hc:", options, nullptr)) != -1)
    {
        switch (arg)
        {
        case 'c':
            g_in_clink_context = 1;
            str_base(app_desc.state_dir).copy(optarg);
            break;

        case '?':
            return 0;

        default:
            show_usage();
            return 0;
        }
    }

    // Dispatch the verb if one was found.
    int ret = 0;
    if (optind < argc)
    {
        app_context app_context(app_desc);
        ret = dispatch_verb(argv[optind], argc - optind, argv + optind);
    }
    else
        show_usage();

    return ret;
}

//------------------------------------------------------------------------------
int loader_main_impl()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 0; i < argc; ++i)
        to_utf8((char*)argv[i], 0xffff, argv[i]);

    int ret = loader(argc, (char**)argv);

    LocalFree(argv);
    return ret;
}
