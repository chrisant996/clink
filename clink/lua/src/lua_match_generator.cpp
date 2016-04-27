// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_match_generator.h"
#include "lua_bindable.h"
#include "lua_script_loader.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "match_builder_lua.h"

#include <lib/line_state.h>
#include <lib/matches.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
lua_match_generator::lua_match_generator(lua_state& state)
: m_state(state)
{
    lua_load_script(m_state, lib, match)
    lua_load_script(m_state, lib, arguments);
}

//------------------------------------------------------------------------------
lua_match_generator::~lua_match_generator()
{
}

//------------------------------------------------------------------------------
void lua_match_generator::print_error(const char* error) const
{
    puts("");
    puts(error);
}

//------------------------------------------------------------------------------
bool lua_match_generator::generate(const line_state& line, match_builder& builder)
{
    lua_State* state = m_state.get_state();

    // Call to Lua to generate matches.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "generate_matches");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    match_builder_lua builder_lua(builder);
    builder_lua.push(state);

    if (lua_pcall(state, 2, 1, 0) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            print_error(error);

        lua_settop(state, 0);
        return false;
    }

    int use_matches = lua_toboolean(state, -1);
    lua_settop(state, 0);

    return !!use_matches;
}
