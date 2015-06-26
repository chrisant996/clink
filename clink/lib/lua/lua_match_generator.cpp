/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "lua_match_generator.h"
#include "lua_script_loader.h"

#include <shared/util.h>

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
match_result lua_match_generator::generate(const char* line, int start, int end)
{
// MODE4
    // Expose some of the readline state to lua.
    lua_createtable(m_state, 2, 0);

    lua_pushliteral(m_state, "line_buffer");
    lua_pushstring(m_state, rl_line_buffer);
    lua_rawset(m_state, -3);

    lua_pushliteral(m_state, "point");
    lua_pushinteger(m_state, rl_point + 1);
    lua_rawset(m_state, -3);

    lua_setglobal(m_state, "rl_state");

    // Call to Lua to generate matches.
    lua_getglobal(m_state, "clink");
    lua_pushliteral(m_state, "generate_matches");
    lua_rawget(m_state, -2);

    lua_pushstring(m_state, line);
    lua_pushinteger(m_state, start + 1);
    lua_pushinteger(m_state, end);
    if (lua_pcall(m_state, 3, 1, 0) != 0)
    {
        puts(lua_tostring(m_state, -1));
        lua_pop(m_state, 2);
        return nullptr;
    }

    int use_matches = lua_toboolean(m_state, -1);
    lua_pop(m_state, 1);

    if (use_matches == 0)
    {
        lua_pop(m_state, 1);
        return nullptr;
    }

    // Collect matches from Lua.
    lua_pushliteral(m_state, "matches");
    lua_rawget(m_state, -2);

    int match_count = (int)lua_rawlen(m_state, -1);
    if (match_count <= 0)
    {
        lua_pop(m_state, 2);
        return nullptr;
    }

    char** matches = (char**)calloc(match_count + 1, sizeof(*matches));
    for (int i = 0; i < match_count; ++i)
    {
        lua_rawgeti(m_state, -1, i + 1);
        const char* match = lua_tostring(m_state, -1);
        matches[i] = (char*)malloc(strlen(match) + 1);
        strcpy(matches[i], match);

        lua_pop(m_state, 1);
    }
    lua_pop(m_state, 2);

    return matches;
// MODE4
}
