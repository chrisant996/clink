/* Copyright (c) 2012 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
int g_in_clink_context;
int inject(int, char**);
int autorun(int, char**);
int set(int, char**);

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
        "set", set
    };

    int i;

    for (i = 0; i < sizeof_array(handlers); ++i)
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
    return -1;
}

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    int arg;
    int ret;

    struct option options[] = {
        { "help",   no_argument,       NULL, 'h' },
        { "cfgdir", required_argument, NULL, 'c' },
        { NULL,     0,                 NULL, 0 }
    };

    // Without arguments, show help.
    if (argc <= 1)
    {
        show_usage();
        return -1;
    }

    // Parse arguments
    while ((arg = getopt_long(argc, argv, "+hc:", options, NULL)) != -1)
    {
        switch (arg)
        {
        case 'c':
            g_in_clink_context = 1;
            set_config_dir_override(optarg);
            break;

        case '?':
            return -1;

        default:
            show_usage();
            return -1;
        }
    }

    // Dispatch the verb if one was found.
    ret = -1;
    if (optind < argc)
    {
        ret = dispatch_verb(argv[optind], argc - optind, argv + optind);
    }
    else
    {
        show_usage();
    }

    return ret;
}

// vim: expandtab
