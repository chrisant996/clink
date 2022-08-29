// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/str_hash.h>
#include <core/str_tokeniser.h>
#include <core/str_compare.h>
#include <lib/matches.h>

//------------------------------------------------------------------------------
/// -name:  string.equalsi
/// -ver:   1.1.20
/// -arg:   a:string
/// -arg:   b:string
/// -ret:   boolean
/// Performs a case insensitive comparison of the strings with international
/// linguistic awareness.  This is more efficient than converting both strings
/// to lowercase and comparing the results.
static int equalsi(lua_State* state)
{
    const char* a = checkstring(state, 1);
    const char* b = checkstring(state, 2);
    if (!a || !b)
        return 0;

    str_compare_scope _(str_compare_scope::caseless, false/*fuzzy_accent*/);
    int result = str_compare(a, b);

    lua_pushboolean(state, result == -1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  string.explode
/// -ver:   1.0.0
/// -arg:   text:string
/// -arg:   [delims:string]
/// -arg:   [quote_pair:string]
/// -ret:   table
/// Splits <span class="arg">text</span> delimited by
/// <span class="arg">delims</span> (or by spaces if not provided) and returns a
/// table containing the substrings.
///
/// The optional <span class="arg">quote_pair</span> can provide a beginning
/// quote character and an ending quote character.  If only one character is
/// provided it is used as both a beginning and ending quote character.
int explode(lua_State* state)
{
    const char* in = checkstring(state, 1);
    const char* delims = optstring(state, 2, " ");
    const char* quote_pair = optstring(state, 3, "");
    if (!in || !delims || !quote_pair)
        return 0;

    str_tokeniser tokens(in, delims);
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
/// -ver:   1.0.0
/// -arg:   text:string
/// -ret:   integer
/// Returns a hash of the input <span class="arg">text</span>.
static int hash(lua_State* state)
{
    const char* in = checkstring(state, 1);
    if (!in)
        return 0;

    lua_pushinteger(state, str_hash(in));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  string.matchlen
/// -ver:   1.1.20
/// -arg:   a:string
/// -arg:   b:string
/// -ret:   integer
/// Returns how many characters match at the beginning of the strings, or -1 if
/// the entire strings match.  This respects the <code>match.ignore_case</code>
/// and <code>match.ignore_accents</code> Clink settings.
/// -show:  string.matchlen("abx", "a")         -- returns 1
/// -show:  string.matchlen("abx", "aby")       -- returns 2
/// -show:  string.matchlen("abx", "abx")       -- returns -1
static int match_len(lua_State* state)
{
    const char* a = checkstring(state, 1);
    const char* b = checkstring(state, 2);
    if (!a || !b)
        return 0;

    int result = str_compare(a, b);
    lua_pushinteger(state, result);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  string.comparematches
/// -ver:   1.3.41
/// -arg:   a:string
/// -arg:   [a_type:string]
/// -arg:   b:string
/// -arg:   [b_type:string]
/// -ret:   boolean
/// Returns true if <span class="arg">a</code> sorts as "less than"
/// <span class="arg">b</code>.
///
/// The <span class="arg">a_type</code> and <span class="arg">b_type</code> are
/// optional, and affect the sort order accordingly when present.
///
/// This produces the same sort order as normally used for displaying matches.
/// This can be used for manually sorting a subset of matches when a match
/// builder has been told not to sort the list of matches.
/// -show:  local files = {
/// -show:  &nbsp;   { match="xyzFile", type="file" },
/// -show:  &nbsp;   { match="abcFile", type="file" },
/// -show:  }
/// -show:  local other = {
/// -show:  &nbsp;   { match="abc_branch", type="arg" },
/// -show:  &nbsp;   { match="xyz_branch", type="arg" },
/// -show:  &nbsp;   { match="mno_tag", type="alias" },
/// -show:  }
/// -show:
/// -show:  local function comparator(a, b)
/// -show:  &nbsp;   return string.comparematches(a.match, a.type, b.match, b.type)
/// -show:  end
/// -show:
/// -show:  -- Sort files.
/// -show:  table.sort(files, comparator)
/// -show:
/// -show:  -- Sort branches and tags together.
/// -show:  table.sort(other, comparator)
/// -show:
/// -show:  match_builder:setnosort()           -- Disable automatic sorting.
/// -show:  match_builder:addmatches(files)     -- Add the sorted files.
/// -show:  match_builder:addmatches(other)     -- Add the branches and tags files.
/// -show:
/// -show:  -- The overall sort order ends up listing all the files in sorted
/// -show:  -- order, followed by branches and tags sorted together.
static int api_compare_matches(lua_State* state)
{
    const bool has_types = lua_isstring(state, 3);
    const char* l = checkstring(state, 1);
    const char* r = checkstring(state, has_types ? 3 : 2);
    if (!l || !r)
        return 0;

    match_type lt;
    match_type rt;
    if (has_types)
    {
        const char* l_type = checkstring(state, 2);
        const char* r_type = checkstring(state, 4);
        if (!l_type || !r_type)
            return 0;
        lt = to_match_type(l_type);
        rt = to_match_type(r_type);
    }
    else
    {
        lt = match_type::none;
        rt = match_type::none;
    }

    const bool less_than = compare_matches(l, lt, r, rt);

    lua_pushboolean(state, less_than);
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
        { "comparematches", &api_compare_matches },
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
