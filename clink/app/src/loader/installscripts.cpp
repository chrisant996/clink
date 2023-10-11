// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "utils/usage.h"

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
static bool change_value(bool install, char** argv=nullptr, int32 argc=0)
{
    if (!argc)
    {
        puts("ERROR: No path provided.");
        return false;
    }

    str<> value;
    for (int32 c = argc; c--;)
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

    str_compare_scope _(str_compare_scope::caseless, false);
    bool uninstalled = false;

    // Check if the provided path is present.
    str_tokeniser tokens(old_list.c_str(), ";");
    str_iter token;
    while (tokens.next(token))
    {
        str_iter tmpt(token); // str_compare advances both iterators, so don't pass token directly!
        str_iter tmpv(value.c_str(), value.length());
        if (str_compare(tmpt, tmpv) == -1)
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
    const char* uni = install ? "i" : "uni";
    const char* Uni = install ? "I" : "Uni";

    static const char* const help_install[] = {
        "-l, --list",   "Lists all installed script paths.",
        "-h, --help",   "Shows this help text.",
        nullptr
    };

    static const char* const help_uninstall[] = {
        "-a, --all",    "Uninstalls all installed script paths.",
        "-l, --list",   "Lists all installed script paths.",
        "-h, --help",   "Shows this help text.",
        nullptr
    };


    puts_clink_header();
    printf("Usage: %snstallscripts <script_path>\n\n", uni);

    puts("Options:");
    puts_help(install ? help_install : help_uninstall);

    printf(
        "%snstalls a script path. This is stored in the registry and applies to\n"
        "all installations of Clink, regardless where their config paths are, etc.\n"
        "This is intended to make it easy for package managers like Scoop to be\n"
        "able to %snstall scripts for use with Clink.\n",
        Uni,
        uni);
}

//------------------------------------------------------------------------------
void list_installed_scripts()
{
    str<> list;
    get_installed_scripts(list);

    str_tokeniser tokens(list.c_str(), ";");
    str_iter token;
    while (tokens.next(token))
        printf("%.*s\n", token.length(), token.get_pointer());
}

//------------------------------------------------------------------------------
int32 installscripts(int32 argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "list", no_argument, nullptr, 'l' },
        { "help", no_argument, nullptr, 'h' },
        {}
    };

    int32 i;
    while ((i = getopt_long(argc, argv, "+?hl", options, nullptr)) != -1)
    {
        switch (i)
        {
        default:
        case '?':
        case 'h':
            print_help(true/*install*/);
            return 0;
        case 'l':
            list_installed_scripts();
            return 0;
        }
    }

    return change_value(true/*install*/, argv + optind, argc - optind) ? 0 : 1;
}

//------------------------------------------------------------------------------
int32 uninstallscripts(int32 argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "help", no_argument, nullptr, 'h' },
        { "list", no_argument, nullptr, 'l' },
        { "all", no_argument, nullptr, 'a' },
        {}
    };

    bool delete_all = false;
    int32 i;
    while ((i = getopt_long(argc, argv, "+?ahl", options, nullptr)) != -1)
    {
        switch (i)
        {
        default:
        case '?':
        case 'h':
            print_help(false/*install*/);
            return 0;
        case 'l':
            list_installed_scripts();
            return 0;
        case 'a':
            // Defer handling via a flag.  This makes all other flags higher
            // precedence, so that `clink uninstallscripts -a -l` will list all
            // rather than delete all, even though -a came first.
            delete_all = true;
            break;
        }
    }

    if (delete_all)
    {
        if (argc - optind > 0)
        {
            fputs("Too many arguments.", stderr);
            return 1;
        }

        if (!set_installed_scripts(""))
        {
            puts("ERROR: Unable to update registry.");
            return 1;
        }

        str<> list;
        get_installed_scripts(list);

        str_tokeniser tokens(list.c_str(), ";");
        str_iter token;
        while (tokens.next(token))
            printf("Script path '%.*s' uninstalled.\n", token.length(), token.get_pointer());
        return 0;
    }

    return change_value(false/*install*/, argv + optind, argc - optind) ? 0 : 1;
}
