// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_integration.h"
#include "editor_module.h"

#include <core/base.h>
#include <terminal/wcwidth.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
}

//------------------------------------------------------------------------------
extern editor_module::result* g_result;

//------------------------------------------------------------------------------
static bool s_force_reload_scripts = false;

//------------------------------------------------------------------------------
bool is_force_reload_scripts()
{
    return s_force_reload_scripts;
}

//------------------------------------------------------------------------------
void clear_force_reload_scripts()
{
    s_force_reload_scripts = false;
}

//------------------------------------------------------------------------------
int32 force_reload_scripts()
{
    s_force_reload_scripts = true;
    if (g_result)
        g_result->done(true); // Force a new edit line so scripts can be reloaded.
    reset_cached_font(); // Force discarding cached font info.
    readline_internal_teardown(true);
    return rl_re_read_init_file(0, 0);
}
