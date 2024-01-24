// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_word_classifier.h"
#include "lua_word_classifications.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "line_states_lua.h"

#include <core/base.h>
#include <lib/line_state.h>
#include <lib/word_classifications.h>

#include <assert.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
line_states_lua::line_states_lua(const line_states& lines)
{
    m_lines.reserve(lines.size());
    for (const auto& line : lines)
        m_lines.emplace_back(line);
}

//------------------------------------------------------------------------------
line_states_lua::line_states_lua(const line_states& lines, word_classifications& classifications)
{
    m_lines.reserve(lines.size());
    m_classifications.reserve(lines.size());
    for (const auto& line : lines)
    {
        m_lines.emplace_back(line);
        m_classifications.emplace_back(classifications, classifications.add_command(line), line.get_command_word_index(), line.get_word_count());
    }
}

//------------------------------------------------------------------------------
void line_states_lua::push(lua_State* state)
{
    // Package the lua objects into a table.
    lua_createtable(state, int32(m_lines.size()), 0);
    for (size_t ii = 0; ii < m_lines.size();)
    {
        lua_createtable(state, 0, 2);

        lua_pushliteral(state, "line_state");
        m_lines[ii].push(state);
        lua_rawset(state, -3);

        if (m_classifications.size())
        {
            lua_pushliteral(state, "classifications");
            m_classifications[ii].push(state);
            lua_rawset(state, -3);
        }

        lua_rawseti(state, -2, int32(++ii));
    }
}

//------------------------------------------------------------------------------
void line_states_lua::make_new(lua_State* state, const line_states& lines)
{
    // Package the lua objects into a table.
    lua_createtable(state, int32(lines.size()), 0);
    for (size_t ii = 0; ii < lines.size();)
    {
        lua_createtable(state, 0, 2);

        lua_pushliteral(state, "line_state");
        line_state_lua::make_new(state, make_line_state_copy(lines[ii]), 0);
        lua_rawset(state, -3);

        lua_rawseti(state, -2, int32(++ii));
    }
}
