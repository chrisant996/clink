// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

extern "C" {
#include <lauxlib.h>
}

#ifdef CLINK_EMBED_LUA_SCRIPTS

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* script)
{
    state.do_string(script);
}

#else // CLINK_EMBED_LUA_SCRIPTS

#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_state& state, const char* path, const char* name)
{
    str<288> buffer;
    buffer << path;

    path::get_directory(buffer);
    path::append(buffer, name);

    state.do_file(buffer.c_str());
}
#endif // CLINK_EMBED_LUA_SCRIPTS
