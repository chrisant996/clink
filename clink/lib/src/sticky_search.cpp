// Copyright (c) 2023 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "sticky_search.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
#include <readline/history.h>
}

//------------------------------------------------------------------------------
static setting_bool g_sticky_search(
    "history.sticky_search",
    "Makes it easy to replay a series of commands",
    "When enabled, reusing a history line does not add the reused line to the end\n"
    "of the history, and it leaves the history search position on the reused line\n"
    "so next/prev history can continue from there (e.g. replaying commands via Up\n"
    "many times, Enter, Down, Enter, Down, Enter, etc).",
    false);

//------------------------------------------------------------------------------
static int32 s_init_history_pos = -1;   // Sticky history position from previous edit line.
static int32 s_history_search_pos = -1; // Most recent history search position during current edit line.

//------------------------------------------------------------------------------
void save_sticky_search_position()
{
    // When 'sticky' mode is enabled, remember the history position for the next
    // input line prompt.
    if (g_sticky_search.get())
    {
        // Favor current history position unless at the end, else favor history
        // search position.  If the search position is invalid or the input line
        // doesn't match the search position, then it works out ok because the
        // search position gets ignored.
        int32 history_pos = where_history();
        if (history_pos >= 0 && history_pos < history_length)
            s_init_history_pos = history_pos;
        else if (s_history_search_pos >= 0 && s_history_search_pos < history_length)
            s_init_history_pos = s_history_search_pos;
        history_prev_use_curr = 1;
    }
    else
        clear_sticky_search_position();
}

//------------------------------------------------------------------------------
void capture_sticky_search_position()
{
    // Temporarily capture the search position (e.g. on each input), so that
    // by the time rl_newline() is invoked the most recent history search
    // position has been cached.
    int32 pos = rl_get_history_search_pos();
    if (pos >= 0)
        s_history_search_pos = pos;
}

//------------------------------------------------------------------------------
void restore_sticky_search_position()
{
    // Apply the remembered history position from the previous command, if any.
    if (s_init_history_pos >= 0)
    {
        history_set_pos(s_init_history_pos);
        history_prev_use_curr = 1;
    }

    s_history_search_pos = -1;
}

//------------------------------------------------------------------------------
bool has_sticky_search_position()
{
    return g_sticky_search.get() && s_init_history_pos >= 0;
}

//------------------------------------------------------------------------------
void clear_sticky_search_position()
{
    s_init_history_pos = -1;
    history_prev_use_curr = 0;
}

//------------------------------------------------------------------------------
static bool history_line_differs(int32 history_pos, const char* line)
{
    const HIST_ENTRY* entry = history_get(history_pos + history_base);
    return (!entry || strcmp(entry->line, line) != 0);
}

//------------------------------------------------------------------------------
bool can_sticky_search_add_history(const char* line)
{
    if (!has_sticky_search_position())
        return true;

    if (s_init_history_pos >= history_length || history_line_differs(s_init_history_pos, line))
        return true;

    // If sticky search is active and the input line matches the history entry
    // at the sticky search position, then use sticky search instead of adding
    // the line to history.
    return false;
}
