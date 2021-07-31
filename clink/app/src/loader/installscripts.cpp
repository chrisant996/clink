// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"

#include <core/base.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>

#include <getopt.h>

//------------------------------------------------------------------------------
void get_installed_scripts(str_base& out);
bool set_installed_scripts(char const* in);

//------------------------------------------------------------------------------
static bool change_value(bool install, char** argv=nullptr, int argc=0)
{
    if (!argc)
    {
        puts("ERROR: No path provided.");
        return false;
    }

    str<> value;
    for (int c = argc; c--;)
    {
        if (value.length())
            value << " ";
        value << *argv;
        argv++;
    }

    if (strchr(value.c_str(), ';'))
    {
        puts("ERROR: Path value contains semicolon; please provide one path at a time.");
        return false;
    }

    str<> new_list;
    str<> old_list;
    get_installed_scripts(old_list);

    str_compare_scope _(str_compare_scope::caseless);
    bool uninstalled = false;

    // Check if the provided path is present.
    str_tokeniser tokens(old_list.c_str(), ";");
    str_iter token;
    while (tokens.next(token))
    {
        str_iter tmpi(value.c_str(), value.length());
        if (str_compare(token, tmpi) == -1)
        {
            // If uninstalling, the path is present, so remove it.
            if (install)
            {
                // If installing, the path is present, so we're done.
                printf("Script path '%s' is already installed.\n", value.c_str());
                return false;
            }
            else
            {
                uninstalled = true;
                continue;
            }
        }
        if (new_list.length())
            new_list << ";";
        new_list.concat(token.get_pointer(), token.length());
    }

    if (!install && !uninstalled)
    {
        printf("Script path '%s' is not installed.\n", value.c_str());
        return false;
    }

    // If installing, add the provided path at the end.
    if (install)
    {
        if (new_list.length())
            new_list << ";";
        new_list << value;
    }

    // Set the new value.
    if (!set_installed_scripts(new_list.c_str()))
    {
        puts("ERROR: Unable to update registry.");
        return false;
    }

    printf("Script path '%s' %sinstalled.\n", value.c_str(), install ? "" : "un");
    return true;
}

//------------------------------------------------------------------------------
static void print_help(bool install)
{
    extern const char* g_clink_header;
    const char* uni = install ? "i" : "uni";
    const char* Uni = install ? "I" : "Uni";

    puts(g_clink_header);
    printf(
        "Usage: %snstallscripts <script_path>\n"
        "\n"
        "%snstalls a script path. This is stored in the registry and applies to\n"
        "all installations of Clink, regardless where their config paths are, etc.\n"
        "This is intended to make it easy for package managers like Scoop to be\n"
        "able to %snstall scripts for use with Clink.\n",
        uni,
        Uni,
        uni);
}

//------------------------------------------------------------------------------
int installscripts(int argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "help", no_argument, nullptr, 'h' },
        {}
    };

    bool complete = false;
    int i;
    while ((i = getopt_long(argc, argv, "+?h", options, nullptr)) != -1)
    {
        switch (i)
        {
        default:
        case '?':
        case 'h':
            print_help(true/*install*/);
            return 0;
        }
    }

    return change_value(true/*install*/, argv + optind, argc - optind) ? 0 : 1;
}

//------------------------------------------------------------------------------
int uninstallscripts(int argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "help", no_argument, nullptr, 'h' },
        { "list", no_argument, nullptr, 'l' },
        {}
    };

    bool complete = false;
    int i;
    while ((i = getopt_long(argc, argv, "+?hl", options, nullptr)) != -1)
    {
        switch (i)
        {
        default:
        case '?':
        case 'h':
            print_help(false/*install*/);
            return 0;
        case 'l':
            {
                str<> list;
                get_installed_scripts(list);

                str_tokeniser tokens(list.c_str(), ";");
                str_iter token;
                while (tokens.next(token))
                    printf("%.*s\n", token.length(), token.get_pointer());
            }
            return 0;
        }
    }

    return change_value(false/*install*/, argv + optind, argc - optind) ? 0 : 1;
}
