// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl/rl_history.h"
#include "utils/app_context.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

//------------------------------------------------------------------------------
void puts_help(const char**, int);

//------------------------------------------------------------------------------
class history_scope
{
public:
                    history_scope(bool save);
                    ~history_scope();
    rl_history*     operator -> ();

private:
    str<280>        m_path;
    rl_history      m_history;
    bool            m_save;
};

//------------------------------------------------------------------------------
history_scope::history_scope(bool save)
: m_save(save)
{
    // Load settings.
    app_context::get()->get_settings_path(m_path);
    settings::load(m_path.c_str());
    
    // Find and load the history
    app_context::get()->get_history_path(m_path);
    m_history.load(m_path.c_str());
}

//------------------------------------------------------------------------------
history_scope::~history_scope()
{
    if (m_save)
        m_history.save(m_path.c_str());
}

//------------------------------------------------------------------------------
rl_history* history_scope::operator -> ()
{
    return &m_history;
}



//------------------------------------------------------------------------------
static void print_history(unsigned int tail_count)
{
    history_scope history(false);

    unsigned int skip = 0;
    if (tail_count)
        skip = max<int>(0, history->get_count() - tail_count);

    history_iter iter;
    iter.skip(skip);

    str<> line;
    while (iter.next(line))
        printf("%5d  %s\n", iter.get_index(), line.c_str());
}

//------------------------------------------------------------------------------
static bool print_history(const char* arg)
{
    if (arg == nullptr)
    {
        print_history(unsigned(0));
        return true;
    }

    // Check the argument is just digits.
    for (const char* c = arg; *c; ++c)
        if (unsigned(*c - '0') > 10)
            return false;

    print_history(atoi(arg));
    return true;
}

//------------------------------------------------------------------------------
static int add(const char* line)
{
    history_scope history(true);
    history->add(line);

    printf("Added '%s' to history.\n", line);
    return 0;
}

//------------------------------------------------------------------------------
static int remove(int index)
{
    history_scope history(true);

    unsigned int length = history->get_count();
    if (index < 0)
        index = length + index;

    bool ok = history->remove(index);

    auto fmt =  ok ?  "Deleted %d.\n" : "Unable to delete history item %d.\n";
    printf(fmt, index);
    return (ok != true);
}

//------------------------------------------------------------------------------
static int clear()
{
    history_scope history(true);
    history->clear();

    puts("History cleared.");
    return 0;
}

//------------------------------------------------------------------------------
static int print_expansion(const char* line)
{
    history_scope history(false);
    str<> out;
    history->expand(line, out);
    puts(out.c_str());
    return 0;
}

//------------------------------------------------------------------------------
static int print_help()
{
    extern const char* g_clink_header;

    const char* help[] = {
        "[n]",          "Print history items (only the last N items if specified).",
        "clear",        "Completly clears the command history.",
        "delete <n>",   "Delete Nth item (negative N indexes history backwards).",
        "add <...>",    "Join remaining arguments and appends to the history.",
        "expand <...>", "Print substitution result.",
    };

    puts(g_clink_header);
    puts("Usage: history <verb> [option]\n");

    puts("Verbs:");
    puts_help(help, sizeof_array(help));

    puts("The 'history' command can also emulates Bash's builtin history command. The\n"
        "arguments -c, -d <n>, -p <...> and -s <...> are supported.\n");

    return 1;
}

//------------------------------------------------------------------------------
static void get_line(int start, int end, char** argv, str_base& out)
{
    for (int j = start; j < end; ++j)
    {
        if (!out.empty())
            out << " ";

        out << argv[j];
    }
}

//------------------------------------------------------------------------------
static int history_bash(int argc, char** argv)
{
    int i;
    while ((i = getopt(argc, argv, "+cd:ps")) != -1)
    {
        switch (i)
        {
        case 'c': // clear history
            return clear();

        case 'd': // remove an item of history
            return remove(atoi(optarg));

        case 'p': // print expansion
        case 's': // add to history
            {
                str<> line;
                get_line(optind, argc, argv, line);
                if (line.empty())
                    return print_help();

                return (i == 's') ? add(line.c_str()) : print_expansion(line.c_str());
            }

        case ':': // option's missing argument.
        case '?': // unknown option.
            return print_help();

        default:
            return -1;
        }
    }

    return -1;
}

//------------------------------------------------------------------------------
int history(int argc, char** argv)
{
    // Check to see if the user asked from some help!
    for (int i = 1; i < argc; ++i)
        if (_stricmp(argv[1], "--help") == 0 || _stricmp(argv[1], "-h") == 0)
            return print_help();

    // Try Bash-style arguments first...
    int bash_ret = history_bash(argc, argv);
    if (optind != 1)
        return bash_ret;

    // ...and the try Clink style arguments.
    if (argc > 1)
    {
        const char* verb = argv[1];

        // 'clear' command
        if (_stricmp(verb, "clear") == 0)
            return clear();

        // 'delete' command
        if (_stricmp(verb, "delete") == 0)
        {
            if (argc < 3)
            {
                puts("history: argument required for verb 'delete'");
                return print_help();
            }
            else
                return remove(atoi(argv[2]));
        }

        str<> line;

        // 'add' command
        if (_stricmp(verb, "add") == 0)
        {
            get_line(2, argc, argv, line);
            return line.empty() ? print_help() : add(line.c_str());
        }

        // 'expand' command
        if (_stricmp(verb, "expand") == 0)
        {
            get_line(2, argc, argv, line);
            return line.empty() ? print_help() : print_expansion(line.c_str());
        }
    }

    // Failing all else try to display the history.
    if (argc > 2)
        return print_help();

    const char* arg = (argc > 1) ? argv[1] : nullptr;
    if (!print_history(arg))
        return print_help();

    return 0;
}
