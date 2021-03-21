// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "utils/seh_scope.h"
#include "version.h"

#include <core/base.h>
#include <core/str.h>

extern "C" {
#include <getopt.h>
}

//------------------------------------------------------------------------------
int autorun(int, char**);
int clink_info(int, char**);
int draw_test(int, char**);
int history(int, char**);
int inject(int, char**);
int input_echo(int, char**);
int set(int, char**);
int installscripts(int, char**);
int uninstallscripts(int, char**);
int testbed(int, char**);

//------------------------------------------------------------------------------
void puts_help(const char* const* help_pairs, int count)
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
    static const char* help_usage = "Usage: [options] <verb> [verb_options]\n";
    static const char* help_verbs[] = {
        "inject",          "Injects Clink into a process",
        "autorun",         "Manage Clink's entry in cmd.exe's autorun",
        "set",             "Adjust Clink's settings",
        "installscripts",  "Add a path to search for scripts",
        "uninstallscripts","Remove a path to search for scripts",
        "history",         "List and operate on the command history",
        "info",            "Prints information about Clink",
        "echo",            "Echo key sequences",
        "",                "('<verb> --help' for more details)",
    };
    static const char* help_options[] = {
        "--profile <dir>", "Use <dir> as Clink's profile directory",
        "--version",       "Print Clink's version and exit",
    };

    extern const char* g_clink_header;

    puts(g_clink_header);
    puts(help_usage);

    puts("Verbs:");
    puts_help(help_verbs, sizeof_array(help_verbs));

    puts("Options:");
    puts_help(help_options, sizeof_array(help_options));
}

//------------------------------------------------------------------------------
static int dispatch_verb(const char* verb, int argc, char** argv)
{
    struct {
        const char* verb;
        int (*handler)(int, char**);
    } handlers[] = {
        "autorun",              autorun,
        "drawtest",             draw_test,
        "echo",                 input_echo,
        "history",              history,
        "info",                 clink_info,
        "inject",               inject,
        "set",                  set,
        "installscripts",       installscripts,
        "uninstallscripts",     uninstallscripts,
        "testbed",              testbed,
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
        { "help",    no_argument,       nullptr, 'h' },
        { "profile", required_argument, nullptr, 'p' },
        { "version", no_argument,       nullptr, 'v' },
        { nullptr,   0,                 nullptr, 0 }
    };

    // Without arguments, show help.
    if (argc <= 1)
    {
        show_usage();
        return 0;
    }

    app_context::desc app_desc;
    app_desc.inherit_id = true;

    // Parse arguments
    int arg;
    while ((arg = getopt_long(argc, argv, "+hp:", options, nullptr)) != -1)
    {
        switch (arg)
        {
        case 'p':
            str_base(app_desc.state_dir).copy(optarg);
            str_base(app_desc.state_dir).trim();
            break;

        case 'v':
            puts(CLINK_VERSION_STR);
            return 0;

        case '?':
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
