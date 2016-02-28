// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "paths.h"

#include <core/str.h>
#include <settings/settings.h>

extern "C" {
#include <readline/history.h>
}

//------------------------------------------------------------------------------
static setting_int g_max_lines(
    "history.max_lines",
    "Lines of history saved to disk",
    "When set to a positive integer this is the number of lines of history\n"
    "that will persist when Clink saves the command history to disk. Use 0\n"
    "for infinite lines and <0 to disable history persistence.",
    10000);

static setting_int g_ignore_space(
    "history.ignore_space",
    "Skip adding lines prefixed with whitespace",
    "Ignore lines that begin with whitespace when adding lines in to\n"
    "the history.",
    0);

static setting_enum g_dupe_mode(
    "history.dupe_mode",
    "Controls how duplicate entries are handled",
    "If a line is a duplicate of an existing history entry Clink will\n"
    "erase the duplicate when this is set 2. A value of 1 will not add\n"
    "duplicates to the history and a value of 0 will always add lines.\n"
    "Note that history is not deduplicated when reading/writing to disk.",
    "add,ignore,erase_dupe",
    2);

static setting_enum g_expand_mode(
    "history.expand_mode",
    "Sets how command history expansion is applied",
    "The '!' character in an entered line can be interpreted to introduce\n"
    "words from the history. This can be enabled and disable by setting this\n"
    "value to 1 or 0. Values or 2, 3 or 4 will skip any ! character quoted\n"
    "in single, double, or both quotes respectively.",
    "off,on,not_squoted,not_dquoted,not_quoted",
    4);

/*static*/ setting_bool g_history_io(
    "history.io",
    "Read/write history file each line edited",
    "When non-zero the history will be read from disk before editing a\n"
    "new line and written to disk afterwards.",
    0);



//------------------------------------------------------------------------------
static int          g_new_history_count             = 0;

//------------------------------------------------------------------------------
static void get_history_file_name(str_base& buffer)
{
    get_config_dir(buffer);
    buffer << "/history";
}

//------------------------------------------------------------------------------
void load_history()
{
    str<512> buffer;
    get_history_file_name(buffer);

    // Clear existing history.
    clear_history();
    g_new_history_count = 0;

    // Read from disk.
    read_history(buffer.c_str());
    using_history();
}

//------------------------------------------------------------------------------
void save_history()
{
    str<512> buffer;
    get_history_file_name(buffer);

    // Get max history size.
    int max_history = g_max_lines.get();
    max_history = (max_history == 0) ? INT_MAX : max_history;
    if (max_history < 0)
    {
        unlink(buffer.c_str());
        return;
    }

    // Write new history to the file, and truncate to our maximum.
    if (g_history_io.get() || append_history(g_new_history_count, buffer.c_str()) != 0)
        write_history(buffer.c_str());

    if (max_history != INT_MAX)
        history_truncate_file(buffer.c_str(), max_history);

    g_new_history_count = 0;
}

//------------------------------------------------------------------------------
static int find_duplicate(const char* line)
{
    HIST_ENTRY* hist_entry;

    using_history();
    while (hist_entry = previous_history())
        if (strcmp(hist_entry->line, line) == 0)
            return where_history();

    return -1;
}

//------------------------------------------------------------------------------
void add_to_history(const char* line)
{
    int dupe_mode;
    const unsigned char* c;

    // Maybe we shouldn't add this line to the history at all?
    c = (const unsigned char*)line;
    if (isspace(*c) && g_ignore_space.get())
        return;

    // Skip leading whitespace
    while (*c)
    {
        if (!isspace(*c))
            break;

        ++c;
    }

    // Skip empty lines
    if (*c == '\0')
        return;

    // Check if the line's a duplicate of and existing history entry.
    dupe_mode = g_dupe_mode.get();
    if (dupe_mode > 0)
    {
        int where = find_duplicate((const char*)c);
        if (where >= 0)
        {
            if (dupe_mode > 1)
            {
                HIST_ENTRY* entry = remove_history(where);
                free_history_entry(entry);
            }
            else
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

    result = history_expand((char*)text, expanded);
    if (result < 0)
        free(*expanded);

    return result;
}

//------------------------------------------------------------------------------
int history_expand_control(char* line, int marker_pos)
{
    int setting, in_quote, i;

    setting = g_expand_mode.get();
    if (setting <= 1)
        return (setting <= 0);

    // Is marker_pos inside a quote of some kind?
    in_quote = 0;
    for (i = 0; i < marker_pos && *line; ++i, ++line)
    {
        int c = *line;
        if (c == '\'' || c == '\"')
            in_quote = (c == in_quote) ? 0 : c;
    }

    switch (setting)
    {
    case 2: return (in_quote == '\'');
    case 3: return (in_quote == '\"');
    case 4: return (in_quote == '\"' || in_quote == '\'');
    }

    return 0;
}
