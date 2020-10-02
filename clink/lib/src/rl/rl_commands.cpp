// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "rl_commands.h"

#include <core/base.h>
#include <core/log.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/history.h>
#include <readline/xmalloc.h>
}



//------------------------------------------------------------------------------
extern line_buffer* rl_buffer;

//------------------------------------------------------------------------------
int clink_reset_line(int count, int invoking_key)
{
    using_history();
    rl_buffer->remove(0, rl_end);
    rl_point = 0;

    return 0;
}

//------------------------------------------------------------------------------
int clink_exit(int count, int invoking_key)
{
    clink_reset_line(1, 0);
    rl_buffer->insert("exit");
    rl_newline(1, invoking_key);

    return 0;
}
