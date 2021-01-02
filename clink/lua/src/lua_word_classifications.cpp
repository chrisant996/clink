// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_word_classifier.h"
#include "lua_word_classifications.h"
#include "lua_state.h"
#include "line_state_lua.h"

#include <lib/line_state.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <assert.h>

//------------------------------------------------------------------------------
static lua_word_classifications::method g_methods[] = {
    { "classifyword",     &lua_word_classifications::classify_word },
    { "iswordclassified", &lua_word_classifications::is_word_classified },
    {}
};



//------------------------------------------------------------------------------
lua_word_classifications::lua_word_classifications(const char* classifications)
: lua_bindable("word_classifications", g_methods)
{
    m_classifications = classifications;
}

//------------------------------------------------------------------------------
int lua_word_classifications::is_word_classified(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;

    int index = int(lua_tointeger(state, 1)) - 1;
    char wc;
    if ((unsigned int)index < m_classifications.length())
        wc = m_classifications.c_str()[index];
    else
        wc = ' ';
    lua_pushboolean(state, wc != ' ');
    return 1;
}

//------------------------------------------------------------------------------
int lua_word_classifications::classify_word(lua_State* state)
{
    if (!lua_isnumber(state, 1) || !lua_isstring(state, 2))
        return 0;

    int index = int(lua_tointeger(state, 1)) - 1;
    const char* s = lua_tostring(state, 2);
    if (!s)
        return 0;

    char wc;
    switch (s[0])
    {
    case 'o':
    case 'c':
    case 'd':
    case 'a':
    case 'f':
    case 'n':
        wc = s[0];
        break;
    default:
        wc = 'o';
        break;
    }

    classify_word(index, wc);
    return 0;
}

//------------------------------------------------------------------------------
bool lua_word_classifications::get_word_class(int word_index_zero_based, word_class& wc) const
{
    if (word_index_zero_based < 0)
        return false;

    if ((unsigned int)word_index_zero_based >= m_classifications.length())
        return false;

    char c = m_classifications.c_str()[word_index_zero_based];
    if (c == ' ')
        return false;

    wc = to_word_class(c);
    return true;
}

//------------------------------------------------------------------------------
void lua_word_classifications::classify_word(int word_index_zero_based, char wc)
{
    assert(word_index_zero_based < 72); // Dubious; word_classifications is a fixed_array of 72.

    if (word_index_zero_based < 0)
        return;

    if ((unsigned int)word_index_zero_based >= m_classifications.length())
    {
        while ((unsigned int)word_index_zero_based >= m_classifications.length())
            m_classifications.concat("                ");
        m_classifications.truncate(word_index_zero_based + 1);
    }

    m_classifications.data()[word_index_zero_based] = wc;
}
