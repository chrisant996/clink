// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include <core/embedded_scripts.h>

#if defined(CLINK_USE_EMBEDDED_SCRIPTS)

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* name, const char* script, int32 length)
{
    assert(!state.is_internal());
    state.set_internal(true);

    state.do_string(script, length, nullptr, name);

    state.set_internal(false);
}

#else // CLINK_USE_EMBEDDED_SCRIPTS

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* path, int32 length)
{
    assert(!state.is_internal());
    state.set_internal(true);

    state.do_file(path);

    state.set_internal(false);
}

#endif // CLINK_USE_EMBEDDED_SCRIPTS
