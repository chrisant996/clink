// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "utils/usage.h"

#include <core/base.h>
#include <core/log.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <lib/history_db.h>
#include <lib/history_timeformatter.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <terminal/ecma48_iter.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctime>
#include <assert.h>

//------------------------------------------------------------------------------
static bool is_console(HANDLE h);
extern setting_bool g_save_history;
extern setting_enum g_history_timestamp;

//------------------------------------------------------------------------------
static bool s_diag = false;
static bool s_showtime = false;
static history_timeformatter s_timeformatter(!is_console(GetStdHandle(STD_OUTPUT_HANDLE)));

//------------------------------------------------------------------------------
class terminal_scope
{
public:
                    terminal_scope(terminal& term);

private:
    printer         m_printer;
    printer_context m_printer_context;
};

//------------------------------------------------------------------------------
terminal_scope::terminal_scope(terminal& term)
: m_printer(*term.out)
, m_printer_context(term.out, &m_printer)
{
}



//------------------------------------------------------------------------------
class history_scope
{
public:
                    history_scope();
                    ~history_scope();
    history_db*     operator -> ()      { return m_history; }
    history_db&     operator * ()       { return *m_history; }

private:
    str<280>        m_path;
    history_db*     m_history;
    terminal        m_terminal;
    terminal_scope* m_terminal_scope;
};

//------------------------------------------------------------------------------
history_scope::history_scope()
{
    // Load settings.
    str<> history_path;
    str<> default_settings_file;
    auto app = app_context::get();
    app->get_settings_path(m_path);
    app->get_history_path(history_path);
    app->get_default_settings_file(default_settings_file);
    settings::load(m_path.c_str(), default_settings_file.c_str());

    m_terminal = terminal_create();
    m_terminal_scope = new terminal_scope(m_terminal);

    if (g_history_timestamp.get() == 2)
        s_showtime = true;

    m_history = new history_db(history_path.c_str(), app->get_id(), g_save_history.get());

    if (s_diag)
        m_history->enable_diagnostic_output();

    str<> msg;
    m_history->initialise(&msg);

    if (msg.length())
        puts(msg.c_str());
}

//------------------------------------------------------------------------------
history_scope::~history_scope()
{
    delete m_terminal_scope;
    terminal_destroy(m_terminal);
}



//------------------------------------------------------------------------------
static bool is_console(HANDLE h)
{
    DWORD dw;
    return !!GetConsoleMode(h, &dw);
}

//------------------------------------------------------------------------------
static void translate_history_line(str_base& out, const char* in, uint32 len)
{
    // Translate control characters.
    for (const char* walk = in; len;)
    {
        const char* begin = walk;
        while (len && (uint8(*walk) >= 0x20 || *walk == 0x09))
            ++walk, --len;
        if (walk > begin)
            out.concat(begin, int32(walk - begin));
        if (!len)
            break;
        char ctrl[3] = { '^', char(*walk + 'A' - 1) };
        out.concat(ctrl, 2);
        ++walk, --len;
    }
}

//------------------------------------------------------------------------------
static void print_history(uint32 tail_count, bool bare)
{
    history_scope history;

    str_iter line;
    history_read_buffer buffer;

    uint32 count = 0;
    uint32 skip = 0;
    if (tail_count != UINT_MAX)
    {
        history_db::iter iter = history->read_lines(buffer.data(), buffer.size());
        while (iter.next(line))
            ++count;
        if (count > tail_count)
            skip = count - tail_count;
    }

    uint32 index = 1;
    history_db::iter iter = history->read_lines(buffer.data(), buffer.size());

    for (uint32 i = 0; i < skip; ++i, ++index, iter.next(line));

    str<> utf8;
    const bool translate = is_console(GetStdHandle(STD_OUTPUT_HANDLE));

    uint32 timelen = 0;
    struct tm tm = {};
    if (s_showtime)
        timelen = s_timeformatter.max_timelen();

    str<32> timestamp;
    uint32 num_from[2] = {};
    for (; iter.next(line, &timestamp); ++index)
    {
        if (s_diag)
        {
            assert(iter.get_bank() < sizeof_array(num_from));
            num_from[iter.get_bank()]++;
        }

        utf8.clear();
        if (!bare)
        {
            utf8.format("%5u  ", index);
            if (s_showtime)
            {
                if (!timestamp.empty())
                {
                    const time_t tt = time_t(atoi(timestamp.c_str()));
                    s_timeformatter.format(tt, timestamp);
                    utf8.concat(timestamp.c_str(), timestamp.length());
                }
                const uint32 len = cell_count(timestamp.c_str());
                if (timelen > len)
                    concat_spaces(utf8, timelen - len);
            }
        }

        if (translate)
        {
            translate_history_line(utf8, line.get_pointer(), line.length());
            utf8.concat("\r\n", 2);
            g_printer->print(utf8.c_str(), utf8.length());
        }
        else
        {
            utf8.concat(line.get_pointer(), line.length());
            puts(utf8.c_str());
        }
    }

    if (s_diag)
    {
        if (history->has_bank(bank_master))
            fprintf(stderr, "... printed %u lines from master bank\n", num_from[bank_master]);
        if (history->has_bank(bank_session))
            fprintf(stderr, "... printed %u lines from session bank\n", num_from[bank_session]);

        // Load history to report diagnostic info about active/deleted lines.
        history->load_rl_history(false/*can_clean*/);
    }
}

//------------------------------------------------------------------------------
static bool print_history(const char* arg, bool bare)
{
    if (arg == nullptr)
    {
        print_history(UINT_MAX, bare);
        return true;
    }

    // Check the argument is just digits.
    uint32 tail_count = 0;
    for (const char* c = arg; *c; ++c)
    {
        if (*c < '0' || *c > '9')
            return false;
        tail_count *= 10;
        tail_count += uint8(*c) - '0';
    }

    print_history(tail_count, bare);
    return true;
}

//------------------------------------------------------------------------------
static int32 add(const char* line)
{
    // NOTE:  This intentionally does not send the "onhistory" Lua event.  The
    // history command doesn't load Lua, and since it explicitly manipulates
    // the history it's reasonable for it to override scripts.

    history_scope history;
    bool ok = history->add(line);

    auto fmt =  ok ?  "Added '%s' to history.\n" : "Unable to add '%s' to history.\n";
    printf(fmt, line);
    return !ok;
}

//------------------------------------------------------------------------------
static int32 remove(int32 index)
{
    history_scope history;

    if (index == 0)
        return 1;

    history_read_buffer buffer;
    history_db::line_id line_id = 0;
    {
        str_iter line;
        history_db::iter iter = history->read_lines(buffer.data(), buffer.size());

        if (index > 0)
        {
            for (int32 i = index - 1; i > 0 && iter.next(line); --i);

            line_id = iter.next(line);
        }
        else
        {
            std::vector<history_db::line_id> lines;
            while (history_db::line_id l = iter.next(line))
                lines.emplace_back(l);

            const size_t li = lines.size() + index;
            if (li >= lines.size())
                return 1;

            line_id = lines[li];
            index = int32(li) + 1;
        }
    }

    bool ok = history->remove(line_id);

    auto fmt =  ok ?  "Deleted item %d.\n" : "Unable to delete history item %d.\n";
    printf(fmt, index);
    return !ok;
}

//------------------------------------------------------------------------------
static int32 clear()
{
    history_scope history;
    history->clear();

    puts("History cleared.");
    return 0;
}

//------------------------------------------------------------------------------
static int32 compact(bool uniq, int32 limit)
{
    history_scope history;
    if (history->has_bank(bank_master))
    {
        history->compact(true/*force*/, uniq, limit);
        puts("History compacted.");
    }
    else
    {
        puts("History is not saved, so compact has nothing to do.");
    }
    return 0;
}

//------------------------------------------------------------------------------
static int32 print_expansion(const char* line)
{
    history_scope history;
    history->load_rl_history(false/*can_clean*/);

    str<> out;
    history->expand(line, out);
    puts(out.c_str());
    return 0;
}

//------------------------------------------------------------------------------
static int32 print_help()
{
    static const char* const help_verbs[] = {
        "[n]",           "Print history items (only the last N items if specified).",
        "clear",         "Completely clears the command history.",
        "compact [n]",   "Compacts the history file.",
        "delete <n>",    "Delete Nth item (negative N indexes history backwards).",
        "add <...>",     "Join remaining arguments and appends to the history.",
        "expand <...>",  "Print substitution result.",
        nullptr
    };

    static const char* const help_options[] = {
        "--bare",        "Omit item numbers and timestamps when printing history.",
        "--diag",        "Print diagnostic info to stderr.",
        "--show-time",   "Show history item timestamps, if any.",
        "--no-show-time",   "Omit history item timestamps when printing history.",
        "--time-format", "Override the format string for showing timestamps.",
        "--unique",      "Remove duplicates when compacting history.",
        nullptr
    };

    puts_clink_header();
    puts("Usage: history <verb> [option]\n");

    puts("Verbs:");
    puts_help(help_verbs, help_options);

    puts("Options:");
    puts_help(help_options, help_verbs);

    puts("The 'history' command can also emulate Bash's builtin history command. The\n"
        "arguments -c, -d <n>, -p <...> and -s <...> are supported.\n");

    puts("The 'history compact' command can shrink the history file by removing any\n"
         "leftover placeholders for deleted items.  Use 'history compact <n>' to also\n"
         "prune the history to no more than N items.");

    return 1;
}

//------------------------------------------------------------------------------
static void get_line(int32 start, int32 end, char** argv, str_base& out)
{
    for (int32 j = start; j < end; ++j)
    {
        if (!out.empty())
            out << " ";

        out << argv[j];
    }
}

//------------------------------------------------------------------------------
static int32 history_bash(int32 argc, char** argv)
{
    int32 i;
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
            optind = -1;
            return print_help();

        default:
            return -1;
        }
    }

    return -1;
}

//------------------------------------------------------------------------------
static bool is_flag(const char* arg, const char* flag, uint32 min_len=-1)
{
    uint32 matched_len = 0;
    while (*arg && *arg == *flag)
    {
        ++arg;
        ++flag;
        ++matched_len;
    }

    if (*arg)
        return false;

    if (*flag && matched_len < min_len)
        return false;

    return true;
}

//------------------------------------------------------------------------------
int32 history(int32 argc, char** argv)
{
    // Check to see if the user asked from some help!
    bool bare = false;
    bool uniq = false;
    for (int32 i = 1; i < argc; ++i)
    {
        if (is_flag(argv[i], "--help", 3) || is_flag(argv[i], "-h") || is_flag(argv[i], "-?"))
            return print_help(), 0;

        int32 remove = 1;
        if (is_flag(argv[i], "--bare", 3))
            bare = true;
        else if (is_flag(argv[i], "--diag", 3))
            s_diag = true;
        else if (is_flag(argv[i], "--unique", 3))
            uniq = true;
        else if (is_flag(argv[i], "--show-time", 3))
            s_showtime = true;
        else if (is_flag(argv[i], "--no-show-time", 3))
            s_showtime = false;
        else if (is_flag(argv[i], "--time-format", 3))
        {
            s_showtime = true;
            s_timeformatter.set_timeformat(argv[++i]);
            remove++;
        }
        else
            remove = 0;

        while (remove--)
        {
            for (int32 j = i; j < argc; ++j)
                argv[j] = argv[j + 1];
            argc--;
            i--;
        }
    }

    // Start logger; but only append, don't reset the log.
    auto* app_ctx = app_context::get();
    if (app_ctx->is_logging_enabled() && !logger::get())
    {
        str<256> log_path;
        app_ctx->get_log_path(log_path);
        new file_logger(log_path.c_str());
    }

    // Try Bash-style arguments first...
    int32 bash_ret = history_bash(argc, argv);
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
        {
            int32 limit = -1;
            if (argc >= 3 && argv[2][0])
            {
                if (argv[2][0] < '0' || argv[2][0] > '9')
                {
                    fputs("history: optional argument for verb 'compact' must be a number", stderr);
                    return print_help();
                }
                limit = atoi(argv[2]);
            }
            return compact(uniq, limit);
        }

        // 'delete' command
        if (_stricmp(verb, "delete") == 0)
        {
            if (argc < 3)
            {
                fputs("history: argument required for verb 'delete'", stderr);
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
    if (!print_history(arg, bare))
        return print_help();

    return 0;
}
