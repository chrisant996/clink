// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/str_hash.h>
#include <core/str_tokeniser.h>
#include <core/str_compare.h>

//------------------------------------------------------------------------------
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
/// -name:  string.equalsi
/// -arg:   a:string
/// -arg:   b:string
/// -ret:   boolean
/// Performs a case insensitive comparison of the strings with international
/// linguistic awareness.  This is more efficient than converting both strings
/// to lowercase and comparing the results.
static int equalsi(lua_State* state)
{
    const char* a = get_string(state, 1);
    const char* b = get_string(state, 2);
    if (!a || !b)
        return 0;

    str_compare_scope _(str_compare_scope::caseless);
    int result = str_compare(a, b);

    lua_pushboolean(state, result == -1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  string.explode
/// -arg:   text:string
/// -arg:   [delims:string]
/// -ret:   table
/// Splits <span class="arg">text</span> delimited by
/// <span class="arg">delims</span> (or by spaces if not provided) and returns a
/// table containing the substrings.
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
/// -name:  string.hash
/// -arg:   text:string
/// -ret:   integer
/// Returns a hash of the input <span class="arg">text</span>.
static int hash(lua_State* state)
{
    const char* in = get_string(state, 1);
    if (in == nullptr)
        return 0;

    lua_pushinteger(state, str_hash(in));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  string.matchlen
/// -arg:   a:string
/// -arg:   b:string
/// -ret:   integer
/// Returns how many characters match at the beginning of the strings, or -1 if
/// the entire strings match.  This respects the <code>match.ignore_case</code>
/// and <code>match.ignore_accents</code> Clink settings.
static int match_len(lua_State* state)
{
    const char* a = get_string(state, 1);
    const char* b = get_string(state, 2);
    if (!a || !b)
        return 0;

    int result = str_compare(a, b);
    lua_pushinteger(state, result);
    return 1;
}

//------------------------------------------------------------------------------
void string_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "equalsi",    &equalsi },
        { "explode",    &explode },
        { "hash",       &hash },
        { "matchlen",   &match_len },
    };

    lua_State* state = lua.get_state();

    lua_getglobal(state, "string");

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pop(state, 1);
}
