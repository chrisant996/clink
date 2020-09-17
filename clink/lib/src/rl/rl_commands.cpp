// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
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
int clink_reset_line(int count, int invoking_key)
{
    using_history();
    rl_delete_text(0, rl_end);
    rl_point = 0;

    return 0;
}
