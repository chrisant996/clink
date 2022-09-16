// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"

#include <core/base.h>
#include <core/log.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <lib/history_db.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctime>
#include <assert.h>

//------------------------------------------------------------------------------
extern setting_bool g_save_history;
extern setting_enum g_history_timestamp;
extern setting_str g_history_timeformat;

//------------------------------------------------------------------------------
extern "C" unsigned int cell_count(const char* in);
void puts_help(const char* const* help_pairs, const char* const* other_pairs=nullptr);

//------------------------------------------------------------------------------
static bool s_diag = false;
static bool s_showtime = false;
static str_moveable s_timeformat;

//------------------------------------------------------------------------------
class history_scope
{
public:
                    history_scope();
    history_db*     operator -> ()      { return m_history; }
    history_db&     operator * ()       { return *m_history; }

private:
    str<280>        m_path;
    history_db*     m_history;
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

    if (g_history_timestamp.get() == 2)
        s_showtime = true;
    if (s_timeformat.empty())
    {
        g_history_timeformat.get(s_timeformat);
        if (s_timeformat.empty())
            s_timeformat = "F% T%  ";
    }

    m_history = new history_db(history_path.c_str(), app->get_id(), g_save_history.get());

    if (s_diag)
        m_history->enable_diagnostic_output();

    str<> msg;
    m_history->initialise(&msg);

    if (msg.length())
        puts(msg.c_str());
}



//------------------------------------------------------------------------------
static bool is_console(HANDLE h)
{
    DWORD dw;
    return !!GetConsoleMode(h, &dw);
}

//------------------------------------------------------------------------------
static void print_history_item(HANDLE hout, const char* utf8, wstr_base* utf16)
{
    if (utf16)
    {
        DWORD written;
        utf16->clear();

        // Translate to UTF16, and also translate control characters.
        for (const char* walk = utf8; *walk;)
        {
            const char* begin = walk;
            while (static_cast<unsigned char>(*walk) >= 0x20 || *walk == 0x09)
                walk++;
            if (walk > begin)
            {
                str_iter tmpi(begin, int(walk - begin));
                to_utf16(*utf16, tmpi);
            }
            if (!*walk)
                break;
            wchar_t ctrl[3] = { '^', wchar_t(*walk + 'A' - 1) };
            utf16->concat(ctrl, 2);
            walk++;
        }

        utf16->concat(L"\r\n", 2);
        WriteConsoleW(hout, utf16->c_str(), utf16->length(), &written, nullptr);
    }
    else
    {
        puts(utf8);
    }
}

//------------------------------------------------------------------------------
static void print_history(unsigned int tail_count, bool bare)
{
    history_scope history;

    str_iter line;
    history_read_buffer buffer;

    unsigned int count = 0;
    unsigned int skip = 0;
    if (tail_count != UINT_MAX)
    {
        history_db::iter iter = history->read_lines(buffer.data(), buffer.size());
        while (iter.next(line))
            ++count;
        if (count > tail_count)
            skip = count - tail_count;
    }

    unsigned int index = 1;
    history_db::iter iter = history->read_lines(buffer.data(), buffer.size());

    for (unsigned int i = 0; i < skip; ++i, ++index, iter.next(line));

    char timebuf[128];
    str<> utf8;
    wstr<> utf16;
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    bool translate = is_console(hout);

    unsigned int timelen = 0;
    struct tm tm = {};
    if (s_showtime)
    {
        tm.tm_year = 2001;
        tm.tm_mon = 11;
        tm.tm_mday = 15;
        tm.tm_hour = 23;
        tm.tm_min = 30;
        tm.tm_sec = 30;
        timebuf[0] = '\0';
        strftime(timebuf, sizeof_array(timebuf), s_timeformat.c_str(), &tm);
        timelen = cell_count(timebuf);
    }

    str<32> timestamp;
    unsigned int num_from[2] = {};
    for (; iter.next(line, &timestamp); ++index)
    {
        if (s_diag)
        {
            assert(iter.get_bank() < sizeof_array(num_from));
            num_from[iter.get_bank()]++;
        }

        utf8.clear();
        if (bare)
            utf8.format("%.*s", line.length(), line.get_pointer());
        else if (!s_showtime)
            utf8.format("%5u  %.*s", index, line.length(), line.get_pointer());
        else
        {
            timebuf[0] = '\0';
            if (!timestamp.empty())
            {
                const time_t tt = time_t(atoi(timestamp.c_str()));
                if (localtime_s(&tm, &tt) == 0)
                    strftime(timebuf, sizeof_array(timebuf), s_timeformat.c_str(), &tm);
            }
            utf8.format("%5u  %-*s%.*s", index, timelen, timebuf, line.length(), line.get_pointer());
        }

        print_history_item(hout, utf8.c_str(), translate ? &utf16 : nullptr);
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
    unsigned int tail_count = 0;
    for (const char* c = arg; *c; ++c)
    {
        if (*c < '0' || *c > '9')
            return false;
        tail_count *= 10;
        tail_count += (unsigned char)*c - '0';
    }

    print_history(tail_count, bare);
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

    history_read_buffer buffer;
    history_db::line_id line_id = 0;
    {
        str_iter line;
        history_db::iter iter = history->read_lines(buffer.data(), buffer.size());
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
static int compact(bool uniq, int limit)
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
static int print_expansion(const char* line)
{
    history_scope history;
    history->load_rl_history(false/*can_clean*/);

    str<> out;
    history->expand(line, out);
    puts(out.c_str());
    return 0;
}

//------------------------------------------------------------------------------
static int print_help()
{
    extern void puts_clink_header();

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
        "--bare",        "Omit item numbers when printing history.",
        "--diag",        "Print diagnostic info to stderr.",
        "--show-time",   "Show history item timestamps, if any.",
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
            optind = -1;
            return print_help();

        default:
            return -1;
        }
    }

    return -1;
}

//------------------------------------------------------------------------------
static bool is_flag(const char* arg, const char* flag, unsigned int min_len=-1)
{
    unsigned int matched_len = 0;
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
int history(int argc, char** argv)
{
    // Check to see if the user asked from some help!
    bool bare = false;
    bool uniq = false;
    for (int i = 1; i < argc; ++i)
    {
        if (is_flag(argv[i], "--help", 3) || is_flag(argv[i], "-h"))
            return print_help();

        int remove = 1;
        if (is_flag(argv[i], "--bare", 3))
            bare = true;
        else if (is_flag(argv[i], "--diag", 3))
            s_diag = true;
        else if (is_flag(argv[i], "--unique", 3))
            uniq = true;
        else if (is_flag(argv[i], "--show-time", 3))
            s_showtime = true;
        else if (is_flag(argv[i], "--time-format", 3))
        {
            s_showtime = true;
            s_timeformat = argv[++i];
            remove++;
        }
        else
            remove = 0;

        while (remove--)
        {
            for (int j = i; j < argc; ++j)
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
        {
            int limit = -1;
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
