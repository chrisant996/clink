// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>

extern "C" {
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
static const char* get_function_name(void* func_addr)
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
    int map_id
)
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
            static const void* blacklist[] = {
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
int show_rl_help(int count, int invoking_key)
{
    char** collector;
    int offset, max;
    Keymap map;
    int i;
    int longest;

    map = rl_get_keymap();
    offset = 1;
    max = 16;
    collector = (char**)malloc(sizeof(char*) * max);
    collector[0] = "";

    // Build string up the functions in the active keymap.
    collector = collect_keymap(map, collector, &offset, &max, 0);
    if (map[ESC].type == ISKMAP && map[ESC].function != nullptr)
    {
        Keymap esc_map = (KEYMAP_ENTRY*)(map[ESC].function);
        collector = collect_keymap(esc_map, collector, &offset, &max, 1);
    }

    if (map == emacs_standard_keymap)
    {
        Keymap ctrlx_map = (KEYMAP_ENTRY*)(map[24].function);
        int type = map[24].type;
        if (type == ISKMAP && ctrlx_map != nullptr)
        {
            collector = collect_keymap(ctrlx_map, collector, &offset, &max, 2);
        }
    }

    // Find the longest match.
    longest = 0;
    for (i = 0; i < offset; ++i)
    {
        int l = (int)strlen(collector[i]);
        if (l > longest)
        {
            longest = l;
        }
    }

    // Display the matches.
    if (rl_completion_display_matches_hook != nullptr)
    {
        rl_filename_completion_desired = 0;
        rl_completion_display_matches_hook(collector, offset - 1, longest);
    }

    // Tidy up (N.B. the first match is a placeholder and shouldn't be freed).
    while (--offset)
    {
        free(collector[offset]);
    }
    free(collector);
    return 0;
}
