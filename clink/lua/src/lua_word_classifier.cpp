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
word_class to_word_class(char ch)
{
    const struct {
        char ch;
        word_class wc;
    } c_lookup[] = {
        { 'o', word_class::other },
        { 'u', word_class::unrecognized },
        { 'x', word_class::executable },
        { 'c', word_class::command },
        { 'd', word_class::doskey },
        { 'a', word_class::arg },
        { 'f', word_class::flag },
        { 'n', word_class::none },
    };
    static_assert(sizeof_array(c_lookup) == int(word_class::max), "c_lookup does not match size of word_class enumeration");

    word_class wc = word_class::other;
    for (const auto& lookup : c_lookup)
        if (lookup.ch == ch)
        {
            wc = lookup.wc;
            break;
        }

    return wc;
}

//------------------------------------------------------------------------------
lua_word_classifier::lua_word_classifier(lua_state& state)
: m_state(state)
{
}

//------------------------------------------------------------------------------
void lua_word_classifier::classify(const line_states& commands, word_classifications& classifications)
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

    m_state.pcall(state, 1, 1);
}
