// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_history.h"
#include "utils/app_context.h"

#include <core/os.h>
#include <core/settings.h>
#include <core/str.h>

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
    30000);

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
static int history_expand_control(char* line, int marker_pos)
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



//------------------------------------------------------------------------------
rl_history::rl_history()
{
    history_inhibit_expansion_function = history_expand_control;

    int max_lines = g_max_lines.get();
    if (max_lines > 0)
        stifle_history(max_lines);
}

//------------------------------------------------------------------------------
rl_history::~rl_history()
{
    clear_history();
}

//------------------------------------------------------------------------------
void rl_history::load(const char* file)
{
    // Clear existing history.
    clear_history();

    // Read from disk.
    read_history(file);
    using_history();
}

//------------------------------------------------------------------------------
void rl_history::save(const char* file)
{
    // Get max history size.
    int max_history = g_max_lines.get();
    if (max_history < 0)
    {
        os::unlink(file);
        return;
    }

    write_history(file);
}

//------------------------------------------------------------------------------
unsigned int rl_history::get_count() const
{
    using_history();
    return where_history();
}

//------------------------------------------------------------------------------
void rl_history::add(const char* line)
{
    // Maybe we shouldn't add this line to the history at all?
    const unsigned char* c = (const unsigned char*)line;
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
    int dupe_mode = g_dupe_mode.get();
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
}

//------------------------------------------------------------------------------
bool rl_history::remove(unsigned int index)
{
    if (index >= get_count())
        return false;

    using_history();
    return (remove_history(index) != nullptr);
}

//------------------------------------------------------------------------------
void rl_history::clear()
{
    clear_history();
}

//------------------------------------------------------------------------------
int rl_history::expand(const char* line, str_base& out) const
{
    char* expanded = nullptr;
    int result = history_expand((char*)line, &expanded);
    if (result >= 0 && expanded != nullptr)
        out.copy(expanded);

    free(expanded);
    return result;
}
