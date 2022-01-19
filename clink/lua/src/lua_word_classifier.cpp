// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_word_classifier.h"
#include "lua_word_classifications.h"
#include "lua_state.h"
#include "line_state_lua.h"

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
word_class to_word_class(char wc)
{
    switch (wc)
    {
    default:    return word_class::other;
    case 'u':   return word_class::unrecognized;
    case 'x':   return word_class::executable;
    case 'c':   return word_class::command;
    case 'd':   return word_class::doskey;
    case 'a':   return word_class::arg;
    case 'f':   return word_class::flag;
    case 'n':   return word_class::none;
    }
}

//------------------------------------------------------------------------------
lua_word_classifier::lua_word_classifier(lua_state& state)
: m_state(state)
{
}

//------------------------------------------------------------------------------
void lua_word_classifier::classify(const std::vector<line_state>& commands, word_classifications& classifications)
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to generate matches.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_classify");
    lua_rawget(state, -2);

    // Build the lua objects for the line_state and word_classifications for
    // each command.
    std::vector<line_state_lua> linestates;
    std::vector<lua_word_classifications> wordclassifications;
    linestates.reserve(commands.size());
    wordclassifications.reserve(commands.size());
    for (const auto& line : commands)
    {
        linestates.emplace_back(line);
        wordclassifications.emplace_back(classifications, classifications.add_command(line), line.get_command_word_index(), line.get_word_count());
    }

    // Package the lua objects into a table.
    lua_createtable(state, int(linestates.size()), 0);
    for (size_t ii = 0; ii < linestates.size();)
    {
        lua_createtable(state, 0, 2);

        lua_pushliteral(state, "line_state");
        linestates[ii].push(state);
        lua_rawset(state, -3);

        lua_pushliteral(state, "classifications");
        wordclassifications[ii].push(state);
        lua_rawset(state, -3);

        lua_rawseti(state, -2, int(++ii));
    }

    if (m_state.pcall(state, 1, 1) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            m_state.print_error(error);
        return;
    }
}
