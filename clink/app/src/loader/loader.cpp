// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "utils/seh_scope.h"
#include "utils/usage.h"
#include "version.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <terminal/ecma48_wrapper.h>

extern "C" {
#include <getopt.h>
}

//------------------------------------------------------------------------------
int32 autorun(int32, char**);
int32 clink_info(int32, char**);
int32 draw_test(int32, char**);
int32 history(int32, char**);
int32 inject(int32, char**, app_context::desc&);
int32 input_echo(int32, char**);
int32 set(int32, char**);
int32 update(int32, char**);
int32 installscripts(int32, char**);
int32 uninstallscripts(int32, char**);
int32 testbed(int32, char**);
int32 interpreter(int32, char**);

//------------------------------------------------------------------------------
bool g_elevated = false;

//------------------------------------------------------------------------------
static void show_usage()
{
    static const char* help_usage = "Usage: [options] <verb> [verb_options]\n";
    static const char* help_verbs[] = {
        "inject",          "Injects Clink into a process.",
        "autorun",         "Manage Clink's entry in cmd.exe's autorun.",
        "set",             "Adjust Clink's settings.",
        "update",          "Check for an update for Clink.",
        "installscripts",  "Add a path to search for scripts.",
        "uninstallscripts","Remove a path to search for scripts.",
        "history",         "List and operate on the command history.",
        "info",            "Prints information about Clink.",
        "echo",            "Echo key sequences for use in .inputrc files.",
        "",                "('<verb> --help' for more details)",
        nullptr
    };
    static const char* help_options[] = {
        "--profile <dir>", "Use <dir> as Clink's profile directory.",
        "--session <id>",  "Override Clink's session id (for history and info).",
        "--version",       "Print Clink's version and exit.",
        nullptr
    };

    puts_clink_header();
    puts(help_usage);

    puts("Verbs:");
    puts_help(help_verbs, help_options);

    puts("Options:");
    puts_help(help_options, help_verbs);
}

//------------------------------------------------------------------------------
static int32 dispatch_verb(const char* verb, int32 argc, char** argv, app_context::desc& desc)
{
    struct {
        const char* verb;
        int32 (*handler)(int32, char**);
        int32 (*handler_nocontext)(int32, char**, app_context::desc&);
    } handlers[] = {
        { "autorun",              autorun,                      },
        { "drawtest",             draw_test,                    },
        { "echo",                 input_echo,                   },
        { "history",              history,                      },
        { "info",                 clink_info,                   },
        { "inject",               nullptr,            inject,   },
        { "set",                  set,                          },
        { "update",               update,                       },
        { "installscripts",       installscripts,               },
        { "uninstallscripts",     uninstallscripts,             },
        { "testbed",              testbed,                      },
        { "lua",                  interpreter,                  },
    };

    for (int32 i = 0; i < sizeof_array(handlers); ++i)
    {
        if (strcmp(verb, handlers[i].verb) == 0)
        {
            int32 ret;
            int32 t;

            t = optind;
            optind = 1;

            if (handlers[i].handler)
            {
                // Allocate app_context from the heap (not stack) so testbed
                // can replace it when simulating an injected scenario.
                app_context* context = new app_context(desc);
                ret = handlers[i].handler(argc, argv);
                delete context;
            }
            else
            {
                ret = handlers[i].handler_nocontext(argc, argv, desc);
            }

            optind = t;
            return ret;
        }
    }

    printf("*** ERROR: Unknown verb -- '%s'\n", verb);
    show_usage();
    return 0;
}

//------------------------------------------------------------------------------
int32 loader(int32 argc, char** argv)
{
    seh_scope seh;

    int32 session = 0;

    struct option options[] = {
        { "help",     no_argument,       nullptr, 'h' },
        { "profile",  required_argument, nullptr, 'p' },
        { "session",  required_argument, nullptr, '~' },
        { "version",  no_argument,       nullptr, 'v' },
        // Undocumented; for internal use by the 'update' command.
        { "elevated", no_argument,       nullptr, '%' },
        { nullptr,    0,                 nullptr, 0 }
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
    int32 arg;
    while ((arg = getopt_long(argc, argv, "+?hp:", options, nullptr)) != -1)
    {
        switch (arg)
        {
        case 'p':
            str_base(app_desc.state_dir).copy(optarg);
            str_base(app_desc.state_dir).trim();
            break;

        case 'v':
            puts(CLINK_VERSION_STR_WITH_BRANCH);
            return 0;

        case '~':
            app_desc.id = atoi(optarg);
            break;

        case '%':
            g_elevated = true;
            break;

        case '?':
        default:
            show_usage();
            return 0;
        }
    }

    // Dispatch the verb if one was found.
    int32 ret = 0;
    if (optind < argc)
        ret = dispatch_verb(argv[optind], argc - optind, argv + optind, app_desc);
    else
        show_usage();

    return ret;
}

//------------------------------------------------------------------------------
int32 loader_main_impl()
{
    int32 argc = 0;
    LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);

    char** argv = static_cast<char**>(calloc(argc + 1, sizeof(char*)));
    for (int32 i = 0; i < argc; ++i)
    {
        const int32 needed = WideCharToMultiByte(CP_UTF8, 0, argvw[i], -1, nullptr, 0, nullptr, nullptr);
        if (!needed)
            return 1;
        argv[i] = static_cast<char*>(malloc(needed));
        WideCharToMultiByte(CP_UTF8, 0, argvw[i], -1, argv[i], needed, nullptr, nullptr);
    }
    assert(argv[argc] == nullptr);

    LocalFree(argvw);

    int32 ret = loader(argc, argv);

    for (int32 i = 0; i < argc; ++i)
        free(argv[i]);
    free(argv);

    return ret;
}
