// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "editor_module.h"

#include <core/base.h>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
}

//------------------------------------------------------------------------------
void editor_module::add_to_rl_macro(const input& input)
{
    if (!RL_ISSTATE(RL_STATE_MACRODEF))
        return;

    const char* keys = input.keys;
    uint32 len = input.len;

    while (len)
    {
        _rl_add_macro_char((uint8)*keys);
        ++keys;
        --len;
    }
}
