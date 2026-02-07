// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_word_classifier.h"
#include "lua_state.h"
#include "line_states_lua.h"

#include <core/base.h>
#include <core/cwd_restorer.h>
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
    static_assert(sizeof_array(c_lookup) == int32(word_class::max), "c_lookup does not match size of word_class enumeration");

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
void lua_word_classifier::classify(const line_states& commands, word_classifications& classifications, bool word_classes)
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to generate matches.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_classify");
    lua_rawget(state, -2);

    line_states_lua lines(commands, classifications);
    lines.push(state);

    lua_pushboolean(state, word_classes);

    os::cwd_restorer cwd;

    m_state.pcall(state, 2, 1);
}
