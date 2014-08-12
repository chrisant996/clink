/* Copyright (c) 2012 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
int                 get_clink_setting_int(const char*);
static int          g_new_history_count             = 0;

//------------------------------------------------------------------------------
static void get_history_file_name(char* buffer, int size)
{
    get_config_dir(buffer, size);
    if (buffer[0])
    {
        str_cat(buffer, "/.history", size);
    }
}

//------------------------------------------------------------------------------
void load_history()
{
    char buffer[512];

    get_history_file_name(buffer, sizeof(buffer));
    read_history(buffer);
    using_history();
}

//------------------------------------------------------------------------------
void save_history()
{
    int max_history;
    char buffer[512];

    get_history_file_name(buffer, sizeof(buffer));

    // Get max history size.
    max_history = get_clink_setting_int("history_file_lines");
    max_history = (max_history == 0) ? INT_MAX : max_history;
    if (max_history < 0)
    {
        unlink(buffer);
        return;
    }

    // Write new history to the file, and truncate to our maximum.
    if (append_history(g_new_history_count, buffer) != 0)
    {
        write_history(buffer);
    }

    history_truncate_file(buffer, max_history);
}

//------------------------------------------------------------------------------
void add_to_history(const char* line)
{
    const unsigned char* c;
    HIST_ENTRY* hist_entry;

    // Skip leading whitespace
    c = (const unsigned char*)line;
    while (*c)
    {
        if (!isspace(*c))
        {
            break;
        }

        ++c;
    }

    // Skip empty lines
    if (*c == '\0')
    {
        return;
    }

    // Check the line's not a duplicate of the last in the history.
    using_history();
    hist_entry = previous_history();
    if (hist_entry != NULL)
    {
        if (strcmp(hist_entry->line, c) == 0)
        {
            return;
        }
    }

    // All's well. Add the line.
    using_history();
    add_history(line);

    ++g_new_history_count;
}

//------------------------------------------------------------------------------
int expand_from_history(const char* text, char** expanded)
{
    int result;

    expanded = NULL;
    result = history_expand(text, &expanded);
    if (result < 0)
    {
        free(expanded);
    }

    return result;
}
