// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_word_classifier.h"
#include "lua_word_classifications.h"
#include "lua_state.h"
#include "line_state_lua.h"

#include <core/base.h>
#include <lib/line_state.h>

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
void lua_word_classifier::classify(const line_state& line, word_classifications& classifications, const char* already_classified)
{
    lua_State* state = m_state.get_state();

    // Call to Lua to generate matches.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_parse_word_types");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    lua_word_classifications classifications_lua(already_classified);
    classifications_lua.push(state);

    if (m_state.pcall(state, 2, 1) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            m_state.print_error(error);

        lua_settop(state, 0);
        return;
    }

    const char* ret = lua_tostring(state, -1);
    lua_settop(state, 0);

    bool has_argmatcher = (ret[0] == 'm');
    if (has_argmatcher)
        ret++;

    const std::vector<word>& words(line.get_words());
    for (unsigned int i = 0; i < strlen(ret); i++)
    {
        word_class_info* info = classifications.push_back();
        info->start = words[i].offset;
        info->end = info->start + words[i].length;
        info->word_class = to_word_class(ret[i]);
        info->argmatcher = (i == 0 && has_argmatcher);
    }

    unsigned int target_count = min<unsigned int>(classifications_lua.size(), line.get_word_count());
    for (unsigned int i = classifications.size(); i < target_count; i++)
    {
        word_class_info* info = classifications.push_back();
        info->start = words[i].offset;
        info->end = info->start + words[i].length;
        info->word_class = word_class::none;
        info->argmatcher = false;
    }

    for (unsigned int i = 0; i < classifications_lua.size(); i++)
    {
        word_class wc;
        if (classifications_lua.get_word_class(i, wc))
            classifications[i]->word_class = wc;
    }
}
