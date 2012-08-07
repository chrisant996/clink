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

#include "clink_pch.h"
#include "shared/clink_util.h"

//------------------------------------------------------------------------------
int inject(int, char**);

//------------------------------------------------------------------------------
static int dispatch_verb(const char* verb, int argc, char** argv)
{
    struct {
        const char* verb;
        int (*handler)(int, char**);
    } handlers[] = {
        "inject", inject
    };

    int i;

    for (i = 0; i < sizeof_array(handlers); ++i)
    {
        if (strcmp(verb, handlers[i].verb) == 0)
        {
            return handlers[i].handler(argc, argv);
        }
    }

    printf("Unknown verb -- '%s'", verb);
    return -1;
}

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    int arg;
    int ret;

    struct option options[] = {
        { "help",   no_argument,    NULL, 'h' },
        { NULL,     0,              NULL, 0 }
    };

    const char* help_usage = "Usage: <verb> <verb_options>\n";
    const char* help_verbs[] = {
        "Verbs:",   "",
        "inject",   "Injects clink into a process.",
        //"autorun",  "Manage clink's entry in cmd.exe's autorun.",
        "",         "('<verb> --help' for more details).",
    };

    extern const char* g_clink_header;
    extern const char* g_clink_footer;

    // Parse arguments
    while ((arg = getopt_long(argc, argv, "+h", options, NULL)) != -1)
    {
        switch (arg)
        {
        case '?':
            return -1;

        default:
            puts(g_clink_header);
            puts(help_usage);
            puts_help(help_verbs, sizeof_array(help_verbs));
            puts(g_clink_footer);
            return -1;
        }
    }

    // Dispatch the verb if one was found.
    ret = -1;
    if (optind < argc)
    {
        ret = dispatch_verb(argv[optind], argc - 1, argv + 1);
    }

    return ret;
}
