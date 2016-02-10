// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

extern "C" {
#include <lauxlib.h>
}

#ifdef CLINK_EMBED_LUA_SCRIPTS

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_State* state, const char* script)
{
    luaL_dostring(state, script);
}

#else // CLINK_EMBED_LUA_SCRIPTS

#include <core/str.h>

#include <algorithm>

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_State* state, const char* path, const char* name)
{
    str<512> buffer;
    buffer << path;

    int slash = std::max(buffer.last_of('\\'), buffer.last_of('/'));
    if (slash >= 0)
    {
        buffer.truncate(slash + 1);
        buffer << name;
        if (luaL_dofile(state, buffer.c_str()) == 0)
            return;
    }

    if (const char* error = lua_tostring(state, -1))
        puts(error);

    printf("CLINK DEBUG: Failed to load '%s'\n", buffer.c_str());
}
#endif // CLINK_EMBED_LUA_SCRIPTS
