// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_match_generator.h"
#include "lua_script_loader.h"
#include "line_state.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

//------------------------------------------------------------------------------
lua_match_generator::lua_match_generator()
: m_state(nullptr)
{
}

//------------------------------------------------------------------------------
lua_match_generator::~lua_match_generator()
{
}

//------------------------------------------------------------------------------
void lua_match_generator::initialise(lua_State* state)
{
    lua_load_script(state, lib, match)
    m_state = state;
}

//------------------------------------------------------------------------------
void lua_match_generator::shutdown()
{
}

//------------------------------------------------------------------------------
void lua_match_generator::generate(const line_state& line, match_result& result)
{
    // Expose some of the readline state to lua.
    lua_createtable(m_state, 2, 0);

    lua_pushliteral(m_state, "line");
    lua_pushstring(m_state, line.line);
    lua_rawset(m_state, -3);

    lua_pushliteral(m_state, "cursor");
    lua_pushinteger(m_state, line.cursor + 1);
    lua_rawset(m_state, -3);

    lua_setglobal(m_state, "line_state");

    // Call to Lua to generate matches.
    lua_getglobal(m_state, "clink");
    lua_pushliteral(m_state, "generate_matches");
    lua_rawget(m_state, -2);

    lua_pushstring(m_state, line.word);
    lua_pushinteger(m_state, line.start + 1);
    lua_pushinteger(m_state, line.end);
    if (lua_pcall(m_state, 3, 1, 0) != 0)
    {
        puts(lua_tostring(m_state, -1));
        lua_pop(m_state, 2);

        file_match_generator::generate(line, result);
        return;
    }

    int use_matches = lua_toboolean(m_state, -1);
    lua_pop(m_state, 1);

    if (use_matches == 0)
    {
        lua_pop(m_state, 1);
        file_match_generator::generate(line, result);
        return;
    }

    // Collect matches from Lua.
    lua_pushliteral(m_state, "matches");
    lua_rawget(m_state, -2);

    int match_count = (int)lua_rawlen(m_state, -1);
    if (match_count <= 0)
        return;

    for (int i = 0; i < match_count; ++i)
    {
        lua_rawgeti(m_state, -1, i + 1);
        const char* match = lua_tostring(m_state, -1);
        result.add_match(match);

        lua_pop(m_state, 1);
    }
    lua_pop(m_state, 2);
}
