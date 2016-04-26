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

    if (state.do_string(buffer.c_str()) == 0)
        return;

    /* MODE4
    if (const char* error = lua_tostring(state, -1))
        puts(error);
    */

    printf("CLINK DEBUG: Failed to load '%s'\n", buffer.c_str());
}
#endif // CLINK_EMBED_LUA_SCRIPTS
