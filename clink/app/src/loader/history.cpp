// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "history/history_db.h"
#include "utils/app_context.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

//------------------------------------------------------------------------------
void puts_help(const char* const*, int);

//------------------------------------------------------------------------------
class history_scope
{
public:
                    history_scope();
    history_db*     operator -> ()      { return &m_history; }
    history_db&     operator * ()       { return m_history; }

private:
    str<280>        m_path;
    history_db      m_history;
};

//------------------------------------------------------------------------------
history_scope::history_scope()
{
    // Load settings.
    app_context::get()->get_settings_path(m_path);
    settings::load(m_path.c_str());

    m_history.initialise();
}



//------------------------------------------------------------------------------
static bool is_console(HANDLE h)
{
    DWORD dw;
    return !!GetConsoleMode(h, &dw);
}

//------------------------------------------------------------------------------
static void print_history(unsigned int tail_count)
{
    history_scope history;

    str_iter line;
    char buffer[history_db::max_line_length];

    int count = 0;
    {
        history_db::iter iter = history->read_lines(buffer);
        while (iter.next(line))
            ++count;
    }

    int index = 1;
    history_db::iter iter = history->read_lines(buffer);

    int skip = count - tail_count;
    for (int i = 0; i < skip; ++i, ++index, iter.next(line));

    str<> utf8;
    wstr<> utf16;
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    bool translate = is_console(hout);

    for (; iter.next(line); ++index)
    {
        utf8.clear();
        utf8.format("%5d  %.*s", index, line.length(), line.get_pointer());
        if (translate)
        {
            DWORD written;
            utf16.clear();

            // Translate to UTF16, and also translate control characters.
            for (const char* walk = utf8.c_str(); *walk;)
            {
                const char* begin = walk;
                while (*walk >= 0x20 || *walk == 0x09)
                    walk++;
                if (walk > begin)
                    to_utf16(utf16, str_iter(begin, int(walk - begin)));
                if (!*walk)
                    break;
                wchar_t ctrl[3] = { '^', wchar_t(*walk + 'A' - 1) };
                utf16.concat(ctrl, 2);
                walk++;
            }

            utf16.concat(L"\r\n", 2);
            WriteConsoleW(hout, utf16.c_str(), utf16.length(), &written, nullptr);
        }
        else
        {
            puts(utf8.c_str());
        }
    }
}

//------------------------------------------------------------------------------
static bool print_history(const char* arg)
{
    if (arg == nullptr)
    {
        print_history(INT_MIN);
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
    history_scope history;
    history->add(line);

    printf("Added '%s' to history.\n", line);
    return 0;
}

//------------------------------------------------------------------------------
static int remove(int index)
{
    history_scope history;

    if (index <= 0)
        return 1;

    char buffer[history_db::max_line_length];
    history_db::line_id line_id = 0;
    {
        str_iter line;
        history_db::iter iter = history->read_lines(buffer);
        for (int i = index - 1; i > 0 && iter.next(line); --i);

        line_id = iter.next(line);
    }

    bool ok = history->remove(line_id);

    auto fmt =  ok ?  "Deleted item %d.\n" : "Unable to delete history item %d.\n";
    printf(fmt, index);
    return (ok != true);
}

//------------------------------------------------------------------------------
static int clear()
{
    history_scope history;
    history->clear();

    puts("History cleared.");
    return 0;
}

//------------------------------------------------------------------------------
static int compact()
{
    history_scope history;
    history->compact();

    puts("History compacted.");
    return 0;
}

//------------------------------------------------------------------------------
static int print_expansion(const char* line)
{
    history_scope history;
    history->load_rl_history(false/*can_limit*/);
    str<> out;
    history->expand(line, out);
    puts(out.c_str());
    return 0;
}

//------------------------------------------------------------------------------
static int print_help()
{
    extern const char* g_clink_header;

    static const char* const help[] = {
        "[n]",          "Print history items (only the last N items if specified).",
        "clear",        "Completely clears the command history.",
        "compact",      "Compacts the history file.",
        "delete <n>",   "Delete Nth item (negative N indexes history backwards).",
        "add <...>",    "Join remaining arguments and appends to the history.",
        "expand <...>", "Print substitution result.",
    };

    puts(g_clink_header);
    puts("Usage: history <verb> [option]\n");

    puts("Verbs:");
    puts_help(help, sizeof_array(help));

    puts("The 'history' command can also emulate Bash's builtin history command. The\n"
        "arguments -c, -d <n>, -p <...> and -s <...> are supported.");

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
    while ((i = getopt(argc, argv, "+?cd:ps")) != -1)
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

        // 'compact' command
        if (_stricmp(verb, "compact") == 0)
            return compact();

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
