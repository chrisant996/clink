// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include <core/base.h>

#if defined(CLINK_USE_EMBEDDED_SCRIPTS)

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* script, int length)
{
    state.do_string(script, length);
}

#else // CLINK_USE_EMBEDDED_SCRIPTS

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* path, int length)
{
    state.do_file(path);
}

#endif // CLINK_USE_EMBEDDED_SCRIPTS
