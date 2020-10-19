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
char *_rl_untranslate_macro_value(char *seq, int use_escapes);
}

#include <assert.h>

//------------------------------------------------------------------------------
extern "C" int _rl_print_completions_horizontally;
extern "C" int _rl_completion_columns;
extern printer* g_printer;
extern pager* g_pager;
extern editor_module::result* g_result;

//------------------------------------------------------------------------------
struct Keyentry
{
    char* key_name;
    char* macro_text;
    const char* func_name;
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
static bool translate_keyseq(const char* keyseq, unsigned int len, char** key_name, bool friendly)
{
    static const char ctrl_map[] = "@abcdefghijklmnopqrstuvwxyz[\\]^_";

    str<> tmp;

    if (!friendly)
    {
        unsigned int comma_threshold = 0;
        for (unsigned int i = 0; i < len; i++)
        {
            if (!i && len == 2 && keyseq[0] == '\x1b')
            {
                comma_threshold++;
                tmp.concat("M-");
                continue;
            }

            if (keyseq[i] >= 0 && keyseq[i] < ' ')
            {
                tmp.concat("C-", 2);
                tmp.concat(&ctrl_map[keyseq[i]], 1);
                continue;
            }

            if (keyseq[i] == 0x7f)
            {
                tmp.concat("Rubout");
                continue;
            }

            tmp.concat(keyseq + i, 1);
        }
    }
    else if (keyseq[0] == 13 && keyseq[1] == 0)
    {
        tmp = "Enter";
    }
    else
    {
        int need_comma = 0;
        while (*keyseq)
        {
            int keyseq_len;
            const char* keyname = find_key_name(keyseq, keyseq_len);
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
                    keyseq++;
                }
                if (*keyseq >= 0 && *keyseq < ' ')
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    tmp.concat("C-", 2);
                    tmp.concat(&ctrl_map[(unsigned char)*keyseq], 1);
                    need_comma = 1;
                    keyseq++;
                }
                else
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    need_comma = 0;

                    if ((unsigned char)*keyseq == 0x7f)
                        tmp.concat("C-Bkspc");
                    else
                        tmp.concat(keyseq, 1);
                    keyseq++;
                }
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
    bool friendly)
{
    int i;
    bool need_sort = (collector == nullptr);

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
            collector = collect_keymap((Keymap)entry.function, collector, offset, max, keyseq, friendly);
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

        if (translate_keyseq(keyseq.c_str(), keyseq.length(), &collector[*offset].key_name, friendly))
        {
            if (entry.type == ISMACR)
                collector[*offset].macro_text = _rl_untranslate_macro_value((char *)entry.function, 0);
            else
                collector[*offset].macro_text = nullptr;

            collector[*offset].func_name = name;
            ++(*offset);
        }

        keyseq.truncate(old_len);
    }

    if (need_sort)
    {
        // TODO: sort using friendly order regardless of the flag
    }

    return collector;
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
    collector = collect_keymap(map, collector, &offset, &max_collect, keyseq, friendly);

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
    int max_width = g_printer->get_columns();
    int columns_that_fit = max_width / longest;
    int columns = max(1, columns_that_fit);
    if (_rl_completion_columns > 0 && columns > _rl_completion_columns)
        columns = _rl_completion_columns;
    int total_rows = ((offset - 1) + (columns - 1)) / columns;

    bool vertical = !_rl_print_completions_horizontally;
    int index_step = vertical ? total_rows : 1;

    // Display the matches.
    str<> str;
    g_printer->print("\n");
    g_pager->start_pager(*g_printer);
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
            str.clear();
            str.format("%-*s : ", longest_key, entry.key_name);
            if (entry.func_name)
                str.concat(entry.func_name);
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
            {
                while (str.length() < longest)
                {
                    const char spaces[] = "                                ";
                    const unsigned int available_spaces = sizeof_array(spaces) - 1;
                    int space_count = min(longest - str.length(), available_spaces);
                    str.concat(spaces, space_count);
                }
            }

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
