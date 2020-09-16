// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_commands.h"

// #include <core/base.h>
// #include <core/log.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/history.h>
#include <readline/xmalloc.h>
}



//------------------------------------------------------------------------------
extern void host_add_history(const char* line);
#if 0
extern void host_remove_history(int n);
#endif

//------------------------------------------------------------------------------
static void clear_line()
{
    using_history();
    rl_delete_text(0, rl_end);
    rl_point = 0;
}

//------------------------------------------------------------------------------
int add_line_to_history(int count, int invoking_key)
{
    add_history(rl_line_buffer);
    host_add_history(rl_line_buffer);
    clear_line();

    return 0;
}

//------------------------------------------------------------------------------
#if 0
int remove_line_from_history(int count, int invoking_key)
{
    HIST_ENTRY* hist;
    int old_where = where_history();

    rl_get_previous_history(1, invoking_key);
    int new_where = where_history();

    hist = remove_history(old_where);
    host_remove_history(old_where);
    free_history_entry(hist);

    if (old_where == new_where)
        clear_line();

    return 0;
}
#endif
