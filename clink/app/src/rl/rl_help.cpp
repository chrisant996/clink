// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <terminal/printer.h>
#include <terminal/terminal.h>
#include "lib/editor_module.h"
#include "lib/pager.h"

extern "C" {
#include <readline/readline.h>
extern char *_rl_untranslate_macro_value(char *seq, int use_escapes);
extern void _rl_move_vert(int);
extern int _rl_vis_botlin;
}

#include <vector>
#include <assert.h>

//------------------------------------------------------------------------------
extern "C" int _rl_print_completions_horizontally;
extern "C" int complete_get_screenwidth(void);
extern printer* g_printer;
extern pager* g_pager;
extern editor_module::result* g_result;

//------------------------------------------------------------------------------
struct Keyentry
{
    int sort;
    char* key_name;
    char* macro_text;
    const char* func_name;
    bool warning;
};

//------------------------------------------------------------------------------
static const char* get_function_name(int (*func_addr)(int, int))
{
    FUNMAP** funcs = funmap;
    while (*funcs != nullptr)
    {
        FUNMAP* func = *funcs;
        if (func->function == func_addr)
            return func->name;

        ++funcs;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
static void concat_key_string(int i, str<32>& keyseq)
{
    assert(i >= 0);
    assert(i < 256);

    char c = (unsigned char)i;
    keyseq.concat(&c, 1);
}

//------------------------------------------------------------------------------
static bool translate_keyseq(const char* keyseq, unsigned int len, char** key_name, bool friendly, int& sort)
{
    static const char ctrl_map[] = "@abcdefghijklmnopqrstuvwxyz[\\]^_";

    str<> tmp;
    int order = 0;
    sort = 0;

    // TODO: Produce identical sort order for both friend names and raw names?

    bool first_key = true;
    if (!friendly)
    {
        tmp << "\"";

        unsigned int comma_threshold = 0;
        for (unsigned int i = 0; i < len; i++)
        {
            if (!i && len == 2 && keyseq[0] == 0x1b)
            {
                comma_threshold++;
                tmp << "\\M-";
                if (first_key)
                    sort |= 4;
                continue;
            }

            char key = keyseq[i];

            if (key == 0x1b)
            {
                tmp << "\\e";
                if (first_key)
                    sort |= 4;
                continue;
            }

            if (key >= 0 && keyseq[i] < ' ')
            {
                tmp << "\\C-";
                tmp.concat(&ctrl_map[key], 1);
                if (first_key)
                    sort |= 2;
                first_key = false;
                continue;
            }

            if (key == RUBOUT)
            {
                tmp << "\\C-?";
                if (first_key)
                    sort |= 2;
                first_key = false;
                continue;
            }

            if (key == '\\' || key == '"')
                tmp << "\\";
            tmp.concat(&key, 1);
            first_key = false;
        }

        tmp << "\"";

        sort <<= 16;
    }
    else
    {
        int need_comma = 0;
        while (*keyseq)
        {
            int keyseq_len;
            int eqclass = 0;
            const char* keyname = find_key_name(keyseq, keyseq_len, eqclass, order);
            if (keyname)
            {
                if (need_comma > 0)
                    tmp.concat(",", 1);
                tmp.concat(keyname);
                need_comma = 1;
                keyseq += keyseq_len;
            }
            else
            {
                if (*keyseq == '\x1b' && len >= 2)
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    need_comma = 0;
                    tmp.concat("A-");
                    eqclass |= 4;
                    keyseq++;
                }
                if (*keyseq >= 0 && *keyseq < ' ')
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    tmp.concat("C-", 2);
                    tmp.concat(&ctrl_map[(unsigned char)*keyseq], 1);
                    eqclass |= 2;
                    need_comma = 1;
                    keyseq++;
                }
                else
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    need_comma = 0;

                    if ((unsigned char)*keyseq == 0x7f)
                    {
                        tmp.concat("C-Bkspc");
                        eqclass |= 2;
                    }
                    else
                    {
                        tmp.concat(keyseq, 1);
                    }
                    keyseq++;
                }
            }

            if (first_key)
            {
                sort = (eqclass << 16) + (order & 0xffff);
                first_key = false;
            }
        }
    }

    if (!tmp.length())
    {
        *key_name = nullptr;
        return false;
    }

    *key_name = (char*)malloc(tmp.length() + 1);
    if (!*key_name)
        return false;

    memcpy(*key_name, tmp.c_str(), tmp.length() + 1);
    return true;
}

//------------------------------------------------------------------------------
static Keyentry* collect_keymap(
    Keymap map,
    Keyentry* collector,
    int* offset,
    int* max,
    str<32>& keyseq,
    bool friendly,
    std::vector<str_moveable>* warnings)
{
    int i;

    for (i = 0; i < 256; ++i)
    {
        KEYMAP_ENTRY entry = map[i];
        if (entry.function == nullptr)
            continue;

        // Recursively chain to another keymap.
        if (entry.type == ISKMAP)
        {
            unsigned int old_len = keyseq.length();
            concat_key_string(i, keyseq);
            collector = collect_keymap((Keymap)entry.function, collector, offset, max, keyseq, friendly, warnings);
            keyseq.truncate(old_len);
            continue;
        }

        // Add entry for a function or macro.
        if (entry.type == ISFUNC)
        {
            int blacklisted;
            int j;

            // Blacklist some functions
            int (*blacklist[])(int, int) = {
                rl_insert,
                rl_do_lowercase_version,
                rl_bracketed_paste_begin,
            };

            blacklisted = 0;
            for (j = 0; j < sizeof_array(blacklist); ++j)
            {
                if (blacklist[j] == entry.function)
                {
                    blacklisted = 1;
                    break;
                }
            }

            if (blacklisted)
                continue;
        }

        const char *name = nullptr;
        char *macro = nullptr;
        if (entry.type == ISFUNC)
        {
            name = get_function_name(entry.function);
            if (name == nullptr)
                continue;
        }

        unsigned int old_len = keyseq.length();
        concat_key_string(i, keyseq);

        if (*offset >= *max)
        {
            *max *= 2;
            collector = (Keyentry *)realloc(collector, sizeof(collector[0]) * *max);
        }

        int sort;
        if (translate_keyseq(keyseq.c_str(), keyseq.length(), &collector[*offset].key_name, friendly, sort))
        {
            collector[*offset].sort = sort;
            if (entry.type == ISMACR)
                collector[*offset].macro_text = _rl_untranslate_macro_value((char *)entry.function, 0);
            else
                collector[*offset].macro_text = nullptr;
            collector[*offset].warning = false;

            if (friendly && warnings && keyseq.length() > 2)
            {
                const char* k = keyseq.c_str();
                if ((k[0] == 'A' || k[0] == 'M' || k[0] == 'C') && (k[1] == '-'))
                {
                    str_moveable s;
                    bool second = (k[2] == 'A' || k[2] == 'M' || k[2] == 'C') && (k[3] == '-');
                    char actual1[4] = { k[0], k[1] };
                    char actual2[4] = { k[2], k[3] };
                    char intent1[4] = { '\\', k[0] == 'A' ? 'M' : k[0], k[1] };
                    char intent2[4] = { '\\', k[2] == 'A' ? 'M' : k[2], k[3] };
                    s << "\x1b[1mwarning:\x1b[m key \x1b[7m" << collector[*offset].key_name << "\x1b[m looks like a typo; did you mean \"" << intent1;
                    if (second)
                        s << intent2;
                    s << "\" instead of \"" << actual1;
                    if (second)
                        s << actual2;
                    s << "\"?";
                    warnings->push_back(std::move(s));
                    collector[*offset].warning = true;
                }
            }

            collector[*offset].func_name = name;
            ++(*offset);
        }

        keyseq.truncate(old_len);
    }

    return collector;
}

//------------------------------------------------------------------------------
static int _cdecl cmp_sort_collector(const void* pv1, const void* pv2)
{
    const Keyentry* p1 = (const Keyentry*)pv1;
    const Keyentry* p2 = (const Keyentry*)pv2;

    // Sort first by modifier keys.
    int cmp = (p1->sort >> 16) - (p2->sort >> 16);
    if (cmp)
        return cmp;

    // Next by named key order.
    cmp = (short int)p1->sort - (short int)p2->sort;
    if (cmp)
        return cmp;

    // Finally sort by key name (folding case).
    cmp = strcmpi(p1->key_name, p2->key_name);
    if (cmp)
        return cmp;
    return strcmp(p1->key_name, p2->key_name);
}

//------------------------------------------------------------------------------
static void pad_with_spaces(str_base& str, unsigned int pad_to)
{
    while (str.length() < pad_to)
    {
        const char spaces[] = "                                ";
        const unsigned int available_spaces = sizeof_array(spaces) - 1;
        int space_count = min(pad_to - str.length(), available_spaces);
        str.concat(spaces, space_count);
    }
}

//------------------------------------------------------------------------------
static void show_key_bindings(bool friendly)
{
    Keymap map = rl_get_keymap();
    int offset = 1;
    int max_collect = 64;
    Keyentry* collector = (Keyentry*)malloc(sizeof(Keyentry) * max_collect);
    collector[0].key_name = nullptr;
    collector[0].macro_text = nullptr;
    collector[0].func_name = nullptr;

    // Build string up the functions in the active keymap.
    str<32> keyseq;
    std::vector<str_moveable> warnings;
    collector = collect_keymap(map, collector, &offset, &max_collect, keyseq, friendly, (map == emacs_standard_keymap) ? &warnings : nullptr);

    // Sort the collected keymap.
    qsort(collector + 1, offset - 1, sizeof(*collector), cmp_sort_collector);

    // Find the longest key name and function name.
    unsigned int longest_key = 0;
    unsigned int longest_func = 0;
    for (int i = 1; i < offset; ++i)
    {
        unsigned int k = (unsigned int)strlen(collector[i].key_name);
        unsigned int f = 0;
        if (collector[i].func_name)
            f = (unsigned int)strlen(collector[i].func_name);
        else if (collector[i].macro_text)
            f = min(2 + (int)strlen(collector[i].macro_text), 32);
        if (longest_key < k)
            longest_key = k;
        if (longest_func < f)
            longest_func = f;
    }

    // Calculate columns.
    unsigned int longest = longest_key + 3 + longest_func + 2;
    int max_width = complete_get_screenwidth();
    int columns_that_fit = max_width / longest;
    int columns = max(1, columns_that_fit);
    int total_rows = ((offset - 1) + (columns - 1)) / columns;

    bool vertical = !_rl_print_completions_horizontally;
    int index_step = vertical ? total_rows : 1;

    // Move cursor past the input line.
    _rl_move_vert(_rl_vis_botlin);

    // Display the matches.
    str<> str;
    g_printer->print("\n");
    g_pager->start_pager(*g_printer);
    if (warnings.size() > 0)
    {
        bool stop = false;

        if (!g_pager->on_print_lines(*g_printer, 1))
            stop = true;
        else
            g_printer->print("\n");

        int num_warnings = stop ? 0 : int(warnings.size());
        for (int i = 0; i < num_warnings; ++i)
        {
            str_moveable& s = warnings[i];

            // Ask the pager what to do.
            int lines = ((s.length() - 14 + max_width - 1) / max_width); // -14 for escape codes.
            if (!g_pager->on_print_lines(*g_printer, lines))
            {
                stop = true;
                break;
            }

            // Print the warning.
            g_printer->print(s.c_str(), s.length());
            g_printer->print("\n");
        }

        if (stop || !g_pager->on_print_lines(*g_printer, 1))
            total_rows = 0;
        else
            g_printer->print("\n");
    }
    for (int i = 0; i < total_rows; ++i)
    {
        int index = vertical ? i : (i * columns);
        index++;

        // Ask the pager what to do.
        int lines = 1;
        if (!columns_that_fit)
        {
            int len = 3; // " : "
            len += int(strlen(collector[index].key_name));
            if (collector[index].func_name)
                len += int(strlen(collector[index].func_name));
            else
                len += min(2 + int(strlen(collector[index].macro_text)), 32);
            lines += len / g_printer->get_columns();
        }
        if (!g_pager->on_print_lines(*g_printer, lines))
            break;

        // Print the row.
        for (int j = columns - 1; j >= 0; --j)
        {
            if (index >= offset)
                continue;

            // Format the key binding.
            const Keyentry& entry = collector[index];
            int escape_code_len = (entry.warning ? 7 : 0);
            str.clear();
            if (entry.warning)
                str << "\x1b[7m";
            str << entry.key_name;
            if (entry.warning)
                str << "\x1b[m";
            pad_with_spaces(str, longest_key + escape_code_len);
            str << " : ";
            if (entry.func_name)
                str << entry.func_name;
            if (entry.macro_text)
            {
                str.concat("\"", 1);
                bool ellipsis = false;
                unsigned int macro_len = (unsigned int)strlen(entry.macro_text);
                if (macro_len > 30)
                {
                    macro_len = 27;
                    ellipsis = true;
                }
                str.concat(entry.macro_text, macro_len);
                if (ellipsis)
                    str.concat("...");
                str.concat("\"", 1);
            }

            // Pad column with spaces.
            if (j)
                pad_with_spaces(str, longest + escape_code_len);

            // Print the key binding.
            g_printer->print(str.c_str(), str.length());

            index += index_step;
        }

        g_printer->print("\n");
    }

    g_printer->print("\n");

    // Tidy up (N.B. the first match is a placeholder and shouldn't be freed).
    while (--offset)
    {
        free(collector[offset].key_name);
        free(collector[offset].macro_text);
    }
    free(collector);

    g_result->redraw();
}

//------------------------------------------------------------------------------
int show_rl_help(int, int)
{
    show_key_bindings(true/*friendly*/);
    return 0;
}

//------------------------------------------------------------------------------
int show_rl_help_raw(int, int)
{
    show_key_bindings(false/*friendly*/);
    return 0;
}
