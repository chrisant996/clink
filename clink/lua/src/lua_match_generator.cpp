// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_match_generator.h"
#include "lua_bindable.h"
#include "lua_script_loader.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "match_builder_lua.h"

#include <core/str_hash.h>
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

#include <unordered_set>

extern void sort_match_list(char** matches, int len);

//------------------------------------------------------------------------------
struct match_hasher
{
    size_t operator()(const char* match) const
    {
        return str_hash(match);
    }
};

//------------------------------------------------------------------------------
struct match_comparator
{
    bool operator()(const char* m1, const char* m2) const
    {
        return strcmp(m1, m2) == 0;
    }
};



//------------------------------------------------------------------------------
lua_match_generator::lua_match_generator(lua_state& state)
: m_state(state)
{
    lua_load_script(m_state, lib, generator);
    lua_load_script(m_state, lib, classifier);
    lua_load_script(m_state, lib, arguments);
}

//------------------------------------------------------------------------------
lua_match_generator::~lua_match_generator()
{
}

//------------------------------------------------------------------------------
bool lua_match_generator::generate(const line_state& line, match_builder& builder)
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

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
            m_state.print_error(error);

        return false;
    }

    int use_matches = lua_toboolean(state, -1);
    return !!use_matches;
}

//------------------------------------------------------------------------------
void lua_match_generator::get_word_break_info(const line_state& line, word_break_info& info) const
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to calculate prefix length.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_get_word_break_info");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    if (m_state.pcall(state, 1, 2) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            m_state.print_error(error);

        info.clear();
        return;
    }

    info.truncate = int(lua_tointeger(state, -2));
    info.keep = int(lua_tointeger(state, -1));
}

//------------------------------------------------------------------------------
static const char* append_string_into_buffer(char*& buffer, const char* match)
{
    const char* ret = buffer;
    if (match)
        while (char c = *(match++))
        {
            if (c == '\r' || c == '\n' || c == '\t')
                c = ' ';
            *(buffer++) = c;
        }
    *(buffer++) = '\0';
    return ret;
}

//------------------------------------------------------------------------------
// Parse ANSI escape codes to determine the visible character length of the
// string (which gets used for column alignment).  Also optionally strip ANSI
// escape codes.
static int plainify(const char* s, bool strip)
{
    int visible_len = 0;

    ecma48_state state;
    ecma48_iter iter(s, state);
    char* plain = const_cast<char *>(s);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            visible_len += code.get_length();
            if (strip)
            {
                const char *ptr = code.get_pointer();
                for (int i = code.get_length(); i--;)
                    *(plain++) = *(ptr++);
            }
        }

    if (strip)
        *plain = '\0';

    return visible_len;
}

//------------------------------------------------------------------------------
bool lua_match_generator::match_display_filter(char** matches, match_display_filter_entry*** filtered_matches, bool popup)
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
    bool ondisplaymatches = false;
    do
    {
        // First check for ondisplaymatches handlers.
        lua_getglobal(state, "clink");
        lua_pushliteral(state, "_has_event_callbacks");
        lua_rawget(state, -2);

        lua_pushliteral(state, "ondisplaymatches");

        if (m_state.pcall(1, 1) != 0)
        {
            if (const char *error = lua_tostring(state, -1))
                m_state.print_error(error);
            goto done;
        }

        ondisplaymatches = (!lua_isnil(state, -1) && lua_toboolean(state, -1) != false);
        lua_pop(state, lua_gettop(state) - top);

        // Popup lists require the new API in order to differentiate between
        // what is displayed and what is inserted.
        if (popup && !ondisplaymatches)
            goto done;

        // NOTE:  If any ondisplaymatches are set, then match_display_filter is
        // not called.  It should be pretty uncommon for a generator to
        // register ondisplaymatches but let generation continue, so I'm
        // comfortable saying that if this loophole is a problem in practice
        // then the affected script should be updated to use the new API.
        lua_getglobal(state, "clink");
        if (ondisplaymatches)
            lua_pushliteral(state, "_send_ondisplaymatches_event");
        else
            lua_pushliteral(state, "match_display_filter");
        lua_rawget(state, -2);

        if (lua_isnil(state, -1))
            goto done;
    }
    while (false);

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
    if (ondisplaymatches)
    {
        str<> tmp;

        for (i = 1; i < match_count; ++i)
        {
            const char* match = matches[i];
            match_type type = match_type::none;
            if (rl_completion_matches_include_type)
            {
                type = match_type(*match);
                match++;
            }

            lua_createtable(state, 2, 0);

            lua_pushliteral(state, "match");
            lua_pushstring(state, match);
            lua_rawset(state, -3);

            lua_pushliteral(state, "type");
            match_type_to_string(type, tmp);
            lua_pushlstring(state, tmp.c_str(), tmp.length());
            lua_rawset(state, -3);

            lua_rawseti(state, -2, i);
        }

        lua_pushboolean(state, popup);
    }
    else
    {
        for (i = 1; i < match_count; ++i)
        {
            const char* match = matches[i];
            if (rl_completion_matches_include_type)
                match++;

            lua_pushstring(state, match);
            lua_rawseti(state, -2, i);
        }
    }

    // Call the filter.
    if (m_state.pcall(ondisplaymatches ? 2 : 1, 1) != 0)
    {
        if (const char *error = lua_tostring(state, -1))
            m_state.print_error(error);
        goto done;
    }

    // Convert table returned by the Lua filter function to C.
    int j = 1;
    bool one_column = false;
    int max_visible_display = 0;
    int max_visible_description = 0;
    int new_len = int(lua_rawlen(state, -1));
    new_matches = (match_display_filter_entry**)calloc(1 + new_len + 1, sizeof(*new_matches));
    new_matches[0] = (match_display_filter_entry*)malloc(sizeof(match_display_filter_entry));
    memset(new_matches[0], 0, sizeof(new_matches[0]));
    new_matches[0]->display = new_matches[0]->buffer;
    for (i = 1; i <= new_len; ++i)
    {
        lua_rawgeti(state, -1, i);
        if (!lua_isnil(state, -1))
        {
            const char* match = nullptr;
            const char* display = nullptr;
            const char* description = nullptr;
            match_type type = match_type::none;

            if (lua_istable(state, -1))
            {
                lua_pushliteral(state, "match");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    match = lua_tostring(state, -1);
                lua_pop(state, 1);

                lua_pushliteral(state, "type");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    type = to_match_type(lua_tostring(state, -1));
                lua_pop(state, 1);

                lua_pushliteral(state, "display");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    display = lua_tostring(state, -1);
                else
                    display = match;
                lua_pop(state, 1);

                lua_pushliteral(state, "description");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    description = lua_tostring(state, -1);
                lua_pop(state, 1);
            }
            else
            {
                display = lua_tostring(state, -1);
            }

            do
            {
                if (!display)
                    break;

                size_t alloc_size = sizeof(match_display_filter_entry) + 2;
                if (match) alloc_size += strlen(match);
                if (display) alloc_size += strlen(display);
                if (description) alloc_size += strlen(description);

                match_display_filter_entry *new_match;
                new_match = (match_display_filter_entry *)malloc(alloc_size);
                memset(new_match, 0, sizeof(*new_match));
                new_match->type = (unsigned char)type;
                new_matches[j] = new_match;

                char* buffer = new_match->buffer;
                j++;

                new_match->match = append_string_into_buffer(buffer, match);
                if (match && !new_match->match[0])
                {
discard:
                    free(new_match);
                    j--;
                }

                if (!display[0])
                    goto discard;
                new_match->display = append_string_into_buffer(buffer, display);
                new_match->visible_display = plainify(new_match->display, popup);
                if (new_match->visible_display <= 0)
                    goto discard;

                if (description)
                {
                    one_column = true;
                    new_match->description = append_string_into_buffer(buffer, description);
                    new_match->visible_description = plainify(new_match->description, popup);
                }

                if (max_visible_display < new_match->visible_display)
                    max_visible_display = new_match->visible_display;
                if (max_visible_description < new_match->visible_description)
                    max_visible_description = new_match->visible_description;
            }
            while (false);
        }

        lua_pop(state, 1);
    }
    new_matches[j] = nullptr;

    if (one_column)
        new_matches[0]->visible_display = 0 - max_visible_display;
    else
        new_matches[0]->visible_display = max_visible_display;
    new_matches[0]->visible_description = max_visible_description;

    *filtered_matches = new_matches;
    ret = true;

done:
    top = lua_gettop(state) - top;
    lua_pop(state, top);
    return ret;
}

//------------------------------------------------------------------------------
void lua_match_generator::filter_matches(char** matches, char completion_type, bool filename_completion_desired)
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Check there's a match filter set.

    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_has_event_callbacks");
    lua_rawget(state, -2);

    lua_pushliteral(state, "onfiltermatches");

    if (m_state.pcall(1, 1) != 0)
    {
        if (const char *error = lua_tostring(state, -1))
            m_state.print_error(error);
        return;
    }

    bool onfiltermatches = (!lua_isnil(state, -1) && lua_toboolean(state, -1) != false);
    if (!onfiltermatches)
        return;

    // Count matches; bail if 0.
    int match_count = 0;
    for (int i = 1; matches[i]; ++i, ++match_count);
    if (match_count <= 0)
        return;

    // Get ready to call the filter function.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_send_onfiltermatches_event");
    lua_rawget(state, -2);
    if (lua_isnil(state, -1))
        return;

    // Convert matches to a Lua table (arg 1).
    str<> tmp;
    lua_createtable(state, match_count, 0);
    for (int i = 1; i < match_count; ++i)
    {
        const char* match = matches[i];
        match_type type = match_type::none;
        if (rl_completion_matches_include_type)
        {
            type = match_type(*match);
            match++;
        }

        lua_createtable(state, 2, 0);

        lua_pushliteral(state, "match");
        lua_pushstring(state, match);
        lua_rawset(state, -3);

        lua_pushliteral(state, "type");
        match_type_to_string(type, tmp);
        lua_pushlstring(state, tmp.c_str(), tmp.length());
        lua_rawset(state, -3);

        lua_rawseti(state, -2, i);
    }

    // Push completion type (arg 2).
    char completion_type_str[2] = { completion_type };
    lua_pushstring(state, completion_type_str);

    // Push filename_completion_desired (arg 3).
    lua_pushboolean(state, filename_completion_desired);

    // Call the filter.
    if (m_state.pcall(3, 1) != 0)
    {
        if (const char *error = lua_tostring(state, -1))
            m_state.print_error(error);
        return;
    }

    // If nil is returned then no filtering occurred.
    if (lua_isnil(state, -1))
        return;

    // Hash the filtered matches to be kept.
    std::unordered_set<const char*, match_hasher, match_comparator> keep_typeless;
    int num_matches = int(lua_rawlen(state, -1));
    for (int i = 1; i <= num_matches; ++i)
    {
        save_stack_top ss(state);

        lua_rawgeti(state, -1, i);
        if (!lua_isnil(state, -1))
        {
            const char* match = nullptr;
            match_type type = match_type::none;

            if (lua_istable(state, -1))
            {
                lua_pushliteral(state, "match");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    match = lua_tostring(state, -1);
                lua_pop(state, 1);

#if 0
                lua_pushliteral(state, "type");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    type = to_match_type(lua_tostring(state, -1));
                lua_pop(state, 1);
#endif
            }
            else
            {
                match = lua_tostring(state, -1);
            }

            if (match)
                keep_typeless.insert(match);
        }
    }

    // Discard other matches.
    bool discarded = false;
    char** read = &matches[1];
    char** write = &matches[1];
    while (*read)
    {
        const char* match = *read;
        if (rl_completion_matches_include_type)
            ++match;

        if (keep_typeless.find(match) == keep_typeless.end())
        {
            discarded = true;
            free(*read);
        }
        else
        {
            if (write != read)
                *write = *read;
            ++write;
        }
        ++read;
    }
    *write = nullptr;

    if (!discarded)
        return;

    extern void reset_generate_matches();
    reset_generate_matches();

    // If no matches, free the lcd as well.
    if (!matches[1])
    {
        free(matches[0]);
        matches[0] = nullptr;
    }
}
