// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#if defined(CLINK_FINAL)

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* script)
{
    state.do_string(script);
}

#else // CLINK_FINAL

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* path)
{
    state.do_file(path);
}

#endif // CLINK_FINAL
