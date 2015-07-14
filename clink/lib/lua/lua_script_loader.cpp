// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

extern "C" {
#include "lualib.h"
}

#ifdef CLINK_EMBED_LUA_SCRIPTS

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_State* state, const char* script)
{
    luaL_dostring(state, script);
}

#else // CLINK_EMBED_LUA_SCRIPTS

#include "core/str.h"

//------------------------------------------------------------------------------
void lua_load_script_impl(lua_State* state, const char* path, const char* name)
{
    str<512> buffer;
    buffer << path;

    int slash = buffer.last_of('\\');
    if (slash < 0)
        slash = buffer.last_of('/');

    if (slash >= 0)
    {
        buffer.truncate(slash + 1);
        buffer << name;
        if (luaL_dofile(state, buffer.c_str()) == 0)
            return;

        if (luaL_dofile(state, name) == 0)
            return;
    }

    printf("CLINK DEBUG: Failed to load '%s'\n", buffer);
}
#endif // CLINK_EMBED_LUA_SCRIPTS
