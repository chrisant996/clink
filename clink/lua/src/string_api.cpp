// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/str_hash.h>
#include <core/str_tokeniser.h>

//------------------------------------------------------------------------------
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
static int hash(lua_State* state)
{
    const char* in = get_string(state, 1);
    if (in == nullptr)
        return 0;

    lua_pushinteger(state, str_hash(in));
    return 1;
}

//------------------------------------------------------------------------------
static int explode(lua_State* state)
{
    const char* in = get_string(state, 1);
    if (in == nullptr)
        return 0;

    const char* delims = get_string(state, 2);
    if (delims == nullptr)
        delims = " ";

    str_tokeniser tokens(in, delims);

    if (const char* quote_pair = get_string(state, 3))
        tokens.add_quote_pair(quote_pair);

    lua_createtable(state, 16, 0);

    int count = 0;
    const char* start;
    int length;
    while (str_token token = tokens.next(start, length))
    {
        lua_pushlstring(state, start, length);
        lua_rawseti(state, -2, ++count);
    }

    return 1;
}

//------------------------------------------------------------------------------
void string_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "hash",       &hash },
        { "explode",    &explode },
    };

    lua_State* state = lua.get_state();

    lua_getglobal(state, "string");

    for (const auto& method : methods)
    {
        lua_pushcfunction(state, method.method);
        lua_setfield(state, -2, method.name);
    }

    lua_pop(state, 1);
}
