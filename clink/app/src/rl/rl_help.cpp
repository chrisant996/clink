// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/settings.h>
#include <terminal/printer.h>
#include "lib/pager.h"

extern "C" {
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
extern "C" int _rl_print_completions_horizontally;
extern "C" int _rl_completion_columns;

//------------------------------------------------------------------------------
static const char* get_function_name(int (*func_addr)(int, int))
{
    FUNMAP** funcs = funmap;
    while (*funcs != nullptr)
    {
        FUNMAP* func = *funcs;
        if (func->function == func_addr)
        {
            return func->name;
        }

        ++funcs;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
static void get_key_string(int i, int map_id, char* buffer)
{
    char* write = buffer;

    switch (map_id)
    {
    case 1: *write++ = 'A'; *write++ = '-'; break;
    case 2: strcpy(buffer, "C-x,"); write += 4; break;
    }

    if (i >= 0 && i < ' ')
    {
        static const char* ctrl_map = "@abcdefghijklmnopqrstuvwxyz[\\]^_";

        *write++ = 'C';
        *write++ = '-';
        *write++ = ctrl_map[i];
        *write++ = '\0';
        return;
    }

    *write++ = i;
    *write++ = '\0';
}

//------------------------------------------------------------------------------
static char** collect_keymap(
    Keymap map,
    char** collector,
    int* offset,
    int* max,
    int map_id)
{
    int i;

    for (i = 0; i < 127; ++i)
    {
        KEYMAP_ENTRY entry = map[i];
        if (entry.type == ISFUNC && entry.function != nullptr)
        {
            int blacklisted;
            int j;

            // Blacklist some functions
            int (*blacklist[])(int, int) = {
                rl_insert,
                rl_do_lowercase_version,
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

            if (!blacklisted)
            {
                char* string;
                const char* name;
                char key[16];

                get_key_string(i, map_id, key);

                name = get_function_name(entry.function);
                if (name == nullptr)
                {
                    continue;
                }

                if (*offset >= *max)
                {
                    *max *= 2;
                    collector = (char**)realloc(collector, sizeof(char*) * *max);
                }

                string = (char*)malloc(strlen(key) + strlen(name) + 32);
                sprintf(string, "%-7s : %s", key, name);

                collector[*offset] = string;
                ++(*offset);
            }
        }
    }

    return collector;
}

//------------------------------------------------------------------------------
void show_rl_help(printer& printer, pager& pager)
{
    Keymap map = rl_get_keymap();
    int offset = 1;
    int max_collect = 64;
    char** collector = (char**)malloc(sizeof(char*) * max_collect);
    collector[0] = nullptr;

    // Build string up the functions in the active keymap.
    collector = collect_keymap(map, collector, &offset, &max_collect, 0);
    if (map[ESC].type == ISKMAP && map[ESC].function != nullptr)
    {
        Keymap esc_map = (KEYMAP_ENTRY*)(map[ESC].function);
        collector = collect_keymap(esc_map, collector, &offset, &max_collect, 1);
    }

    if (map == emacs_standard_keymap)
    {
        Keymap ctrlx_map = (KEYMAP_ENTRY*)(map[24].function);
        int type = map[24].type;
        if (type == ISKMAP && ctrlx_map != nullptr)
        {
            collector = collect_keymap(ctrlx_map, collector, &offset, &max_collect, 2);
        }
    }

    // Find the longest match.
    int longest = 0;
    for (int i = 1; i < offset; ++i)
    {
        int l = (int)strlen(collector[i]);
        if (l > longest)
            longest = l;
    }

    // Display the matches.
    printer.print("\n");
    pager.start_pager(printer);

    int max_width = printer.get_columns() - 3;
    int columns_that_fit = max_width / (longest + 1);
    int columns = max(1, columns_that_fit);
    if (_rl_completion_columns > 0 && columns > _rl_completion_columns)
        columns = _rl_completion_columns;
    int total_rows = (offset + columns - 1) / columns;

    bool vertical = !_rl_print_completions_horizontally;
    int index_step = vertical ? total_rows : 1;

    for (int i = 1; i < total_rows; ++i)
    {
        int index = vertical ? i : (i * columns);

        // Ask the pager what to do.
        const int lines = 1 + (columns_that_fit ? 0 : int(strlen(collector[index]) / printer.get_columns()));
        if (!pager.on_print_lines(printer, lines))
            break;

        // Print the row.
        for (int j = columns - 1; j >= 0; --j)
        {
            if (index >= offset)
                continue;

            // Print the key binding.
            const char* match = collector[index];

            int length = int(strlen(match));
            printer.print(match, length);

            const char spaces[] = "                                         ";
            int space_count = max(longest - length, 0) + 1;
            printer.print(spaces, min<int>(sizeof(spaces) - 1, space_count));

            index += index_step;
        }

        printer.print("\n");
    }

    printer.print("\n");

    // Tidy up (N.B. the first match is a placeholder and shouldn't be freed).
    while (--offset)
        free(collector[offset]);
    free(collector);
}
