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
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
lua_match_generator::lua_match_generator(lua_state& state)
: m_state(state)
{
    lua_load_script(m_state, lib, generator);
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

    // Backward compatibility shim.
    if (true)
    {
        // Expose some of the readline state to lua.
        lua_createtable(state, 2, 0);

        lua_pushliteral(state, "line_buffer");
        lua_pushstring(state, rl_line_buffer);
        lua_rawset(state, -3);

        lua_pushliteral(state, "point");
        lua_pushinteger(state, rl_point + 1);
        lua_rawset(state, -3);

        lua_setglobal(state, "rl_state");
    }

    // Call to Lua to generate matches.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_generate");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    match_builder_lua builder_lua(builder);
    builder_lua.push(state);

    if (m_state.pcall(state, 2, 1) != 0)
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

//------------------------------------------------------------------------------
int lua_match_generator::get_prefix_length(const line_state& line) const
{
    lua_State* state = m_state.get_state();

    // Call to Lua to calculate prefix length.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_get_prefix_length");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    if (m_state.pcall(state, 1, 1) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            print_error(error);

        lua_settop(state, 0);
        return 0;
    }

    int prefix = int(lua_tointeger(state, -1));
    lua_settop(state, 0);
    return prefix;
}
