// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "suggest.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/os.h>
#include <lib/line_state.h>
#include "lua_script_loader.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "matches_lua.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
suggester::suggester(lua_state& lua)
: m_lua(lua)
{
    lua_load_script(lua, app, suggest);
}

//------------------------------------------------------------------------------
void suggester::suggest(line_state& line, matches& matches, str_base& out)
{
    lua_State* state = m_lua.get_state();

    int top = lua_gettop(state);

    // Call Lua to filter prompt
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_suggest");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    matches_lua matches_lua(matches);
    matches_lua.push(state);

    if (m_lua.pcall(state, 2, 1) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            m_lua.print_error(error);
        lua_pop(state, 2);
        return;
    }

    // Collect the suggestion.
    const char* suggestion = lua_tostring(state, -1);
    out = suggestion;

    lua_settop(state, top);
}
