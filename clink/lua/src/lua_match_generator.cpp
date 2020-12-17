// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_match_generator.h"
#include "lua_bindable.h"
#include "lua_script_loader.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "match_builder_lua.h"

#include <lib/line_state.h>
#include <lib/matches.h>
#include <terminal/ecma48_iter.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <readline/readline.h>
#include <compat/display_matches.h>
}

extern void sort_match_list(char** matches, int len);

//------------------------------------------------------------------------------
lua_match_generator::lua_match_generator(lua_state& state)
: m_state(state)
{
    lua_load_script(m_state, lib, generator);
    lua_load_script(m_state, lib, arguments);
}

//------------------------------------------------------------------------------
lua_match_generator::~lua_match_generator()
{
}

//------------------------------------------------------------------------------
void lua_match_generator::print_error(const char* error) const
{
    puts("");
    puts(error);
}

//------------------------------------------------------------------------------
bool lua_match_generator::generate(const line_state& line, match_builder& builder)
{
    lua_State* state = m_state.get_state();

    // Backward compatibility shim.
    if (true)
    {
        // Expose some of the readline state to lua.
        lua_createtable(state, 2, 0);

        lua_pushliteral(state, "line_buffer");
        lua_pushstring(state, rl_line_buffer);
        lua_rawset(state, -3);

        lua_pushliteral(state, "point");
        lua_pushinteger(state, rl_point + 1);
        lua_rawset(state, -3);

        lua_setglobal(state, "rl_state");
    }

    // Call to Lua to generate matches.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_generate");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    match_builder_lua builder_lua(builder);
    builder_lua.push(state);

    if (m_state.pcall(state, 2, 1) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            print_error(error);

        lua_settop(state, 0);
        return false;
    }

    int use_matches = lua_toboolean(state, -1);
    lua_settop(state, 0);

    return !!use_matches;
}

//------------------------------------------------------------------------------
void lua_match_generator::get_word_break_info(const line_state& line, word_break_info& info) const
{
    lua_State* state = m_state.get_state();

    // Call to Lua to calculate prefix length.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_get_word_break_info");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    if (m_state.pcall(state, 1, 2) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            print_error(error);

        lua_settop(state, 0);
        info.clear();
        return;
    }

    info.truncate = int(lua_tointeger(state, -2));
    info.keep = int(lua_tointeger(state, -1));
    lua_settop(state, 0);
}

//------------------------------------------------------------------------------
bool lua_match_generator::match_display_filter(char** matches, match_display_filter_entry*** filtered_matches)
{
    bool ret = false;
    lua_State* state = m_state.get_state();

    // A small note about the contents of 'matches' - the first match isn't
    // really a match, it's the word being completed. Readline ignores it when
    // displaying the matches. So matches[1...n] are useful.

    match_display_filter_entry** new_matches = nullptr;
    int top = lua_gettop(state);
    int i;

    // Check there's a display filter set.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "match_display_filter");
    lua_rawget(state, -2);
    if (lua_isnil(state, -1))
        goto done;

    // If the caller just wants to know whether a display filter is active, then
    // short circuit.
    if (!matches || !filtered_matches)
    {
        if (filtered_matches)
            *filtered_matches = nullptr;
        ret = true;
        goto done;
    }

    // Count matches.
    int match_count = 0;
    for (i = 1; matches[i]; ++i, ++match_count);

    // Sort the matches.
    sort_match_list(matches + 1, match_count);

    // Convert matches to a Lua table.
    lua_createtable(state, match_count, 0);
    for (i = 1; i < match_count; ++i)
    {
        const char* match = matches[i];
        if (rl_completion_matches_include_type)
            match++;

        lua_pushstring(state, match);
        lua_rawseti(state, -2, i);
    }

    // TODO: match types?

    // Call the filter.
    if (m_state.pcall(1, 1) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            print_error(error);
        goto done;
    }

    // TODO: some way for the filter to indicate it wants matches to use only one column.
    bool one_column = false;

    // Convert table returned by the Lua filter function to C.
    int j = 1;
    bool force_one_column = false;
    int new_len = int(lua_rawlen(state, -1));
    new_matches = (match_display_filter_entry**)calloc(1 + new_len + 1, sizeof(*new_matches));
    new_matches[0] = (match_display_filter_entry*)malloc(sizeof(match_display_filter_entry));
    new_matches[0]->visible_len = one_column ? -1 : 0;
    new_matches[0]->match[0] = '\0';
    for (i = 1; i <= new_len; ++i)
    {
        lua_rawgeti(state, -1, i);
        if (!lua_isnil(state, -1))
        {
            const char* match = lua_tostring(state, -1);
            if (match)
            {
                match_display_filter_entry *new_match;
                new_match = (match_display_filter_entry *)malloc(sizeof(match_display_filter_entry) + strlen(match));
                new_matches[j] = new_match;

                for (char *to = new_match->match; true; to++, match++)
                {
                    char c = *match;
                    *to = c;
                    if (!c)
                        break;
                    if (c == '\r' || c == '\n' || c == '\t')
                        *to = ' ';
                }
                j++;

                // Parse ANSI escape codes to determine the visible character
                // length of the match (which gets used for column alignment).
                ecma48_state state;
                ecma48_iter iter(new_match->match, state);
                int visible_len = 0;
                while (const ecma48_code &code = iter.next())
                    if (code.get_type() == ecma48_code::type_chars)
                        visible_len += code.get_length();
                new_match->visible_len = visible_len;
            }
        }

        lua_pop(state, 1);
    }
    new_matches[j] = nullptr;

    if (force_one_column)
        new_matches[0]->visible_len = -1;

    *filtered_matches = new_matches;
    ret = true;

done:
    top = lua_gettop(state) - top;
    lua_pop(state, top);
    return ret;
}
