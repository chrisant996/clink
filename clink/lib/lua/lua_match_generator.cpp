// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_match_generator.h"
#include "line_state.h"
#include "lua_bindable.h"
#include "lua_script_loader.h"
#include "matches_lua.h"

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
void lua_match_generator::lua_pushlinestate(const line_state& line)
{
    lua_pushstring(m_state, line.word);
    lua_pushinteger(m_state, line.start + 1);
    lua_pushinteger(m_state, line.end);
}

//------------------------------------------------------------------------------
void lua_match_generator::print_error(const char* error) const
{
    puts("");
    puts(error);
}

//------------------------------------------------------------------------------
void lua_match_generator::generate(const line_state& line, matches& result)
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

    lua_pushlinestate(line);

    matches_lua ml(result);
    ml.lua_bind(m_state);
    ml.lua_push(m_state);

    if (lua_pcall(m_state, 4, 1, 0) != 0)
    {
        if (const char* error = lua_tostring(m_state, -1))
            print_error(error);

        lua_settop(m_state, 0);
        file_match_generator::generate(line, result);
        return;
    }

    ml.lua_unbind(m_state);

    int use_matches = lua_toboolean(m_state, -1);
    lua_settop(m_state, 0);

    if (use_matches)
        return;

    result.clear_matches();
    file_match_generator::generate(line, result);
}
