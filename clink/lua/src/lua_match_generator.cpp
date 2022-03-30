// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_match_generator.h"
#include "lua_bindable.h"
#include "lua_script_loader.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "line_states_lua.h"
#include "match_builder_lua.h"

#include <core/str_hash.h>
#include <core/str_unordered_set.h>
#include <core/str_compare.h>
#include <core/os.h>
#include <lib/line_state.h>
#include <lib/matches.h>
#include <lib/matches_lookaside.h>
#include <lib/popup.h>
#include <lib/display_matches.h>
#include <terminal/ecma48_iter.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <readline/readline.h>
char* printable_part (char* pathname);
}

#include <unordered_set>

//------------------------------------------------------------------------------
lua_match_generator::lua_match_generator(lua_state& state)
: m_state(state)
{
}

//------------------------------------------------------------------------------
lua_match_generator::~lua_match_generator()
{
}

//------------------------------------------------------------------------------
bool lua_match_generator::generate(const line_states& lines, match_builder& builder, bool old_filtering)
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to generate matches.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_generate");
    lua_rawget(state, -2);

    line_state_lua line_lua(lines.back());
    line_lua.push(state);

    line_states_lua lines_lua(lines);
    lines_lua.push(state);

    match_builder_lua builder_lua(builder);
    builder_lua.push(state);

    lua_pushboolean(state, old_filtering);

    os::cwd_restorer cwd;

    if (m_state.pcall(state, 4, 1) != 0)
        return false;

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

    os::cwd_restorer cwd;

    if (m_state.pcall(state, 1, 2) != 0)
    {
        info.clear();
        return;
    }

    info.truncate = int(lua_tointeger(state, -2));
    info.keep = int(lua_tointeger(state, -1));
}

//------------------------------------------------------------------------------
// Parse ANSI escape codes to determine the visible character length of the
// string (which gets used for column alignment).  When a strip out parameter is
// supplied, this also strips ANSI escape codes and the strip out parameter
// receives a pointer to the next character past the nul terminator.
static int plainify(const char* s, char** strip)
{
    int visible_len = 0;

    // TODO:  This does not handle BEL, OSC title codes, or envvar substitution.
    // Use ecma48_processor() if that becomes necessary, but then s cannot be
    // in/out since envvar substitutions could make the output string be longer
    // than the input string.

    ecma48_state state;
    ecma48_iter iter(s, state);
    char* plain = const_cast<char *>(s);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (int c = inner_iter.next())
                visible_len += clink_wcwidth(c);

            if (strip)
            {
                const char *ptr = code.get_pointer();
                for (int i = code.get_length(); i--;)
                    *(plain++) = *(ptr++);
            }
        }

    if (strip)
    {
        *(plain++) = '\0';
        *strip = plain;
    }

    return visible_len;
}

//------------------------------------------------------------------------------
bool lua_match_generator::match_display_filter(const char* needle, char** matches, match_display_filter_entry*** filtered_matches, display_filter_flags flags, bool nosort, bool* old_filtering)
{
    bool ret = false;
    lua_State* state = m_state.get_state();
    str<> lcd;
    bool lcd_initialized = false;
    const bool selectable = (flags & display_filter_flags::selectable) == display_filter_flags::selectable;
    const bool strip_markup = (flags & display_filter_flags::plainify) == display_filter_flags::plainify;

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
            goto done;

        ondisplaymatches = (!lua_isnil(state, -1) && lua_toboolean(state, -1) != false);
        lua_pop(state, lua_gettop(state) - top);

        // Popup lists require the new API in order to differentiate between
        // what is displayed and what is inserted.
        if (selectable && !ondisplaymatches)
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
success:
        ret = true;
        if (old_filtering)
            *old_filtering = !ondisplaymatches;
done:
        top = lua_gettop(state) - top;
        lua_pop(state, top);
        return ret;
    }

    needle = printable_part(const_cast<char*>(needle));
    const int needle_len = int(strlen(needle));

    // Count matches.
    const bool only_lcd = matches[0] && !matches[1];
    int match_count = only_lcd ? 1 : 0;
    for (i = 1; matches[i]; ++i, ++match_count);

    // Convert matches to a Lua table.
    lua_createtable(state, match_count, 0);
    if (ondisplaymatches)
    {
        str<> tmp;

        int mi = only_lcd ? 0 : 1;
        for (i = 1; i <= match_count; ++i)
        {
            const char* match = matches[mi++];
            match_details details = lookup_match(match);

            lua_createtable(state, 0, 2);

            lua_pushliteral(state, "match");
            lua_pushstring(state, match);
            lua_rawset(state, -3);

            lua_pushliteral(state, "type");
            match_type_to_string(details.get_type(), tmp);
            lua_pushlstring(state, tmp.c_str(), tmp.length());
            lua_rawset(state, -3);

            const char* display = details.get_display();
            if (display && *display && display != match)
            {
                lua_pushliteral(state, "display");
                lua_pushstring(state, display);
                lua_rawset(state, -3);
            }

            const char* description = details.get_description();
            if (description && *description)
            {
                lua_pushliteral(state, "description");
                lua_pushstring(state, description);
                lua_rawset(state, -3);
            }

            char append_char = details.get_append_char();
            if (append_char)
            {
                lua_pushliteral(state, "appendchar");
                lua_pushlstring(state, &append_char, 1);
                lua_rawset(state, -3);
            }

            unsigned char flags = details.get_flags();
            if (flags & MATCH_FLAG_HAS_SUPPRESS_APPEND)
            {
                lua_pushliteral(state, "suppressappend");
                lua_pushboolean(state, !!(flags & MATCH_FLAG_SUPPRESS_APPEND));
                lua_rawset(state, -3);
            }

            lua_rawseti(state, -2, i);
        }

        lua_pushboolean(state, selectable); // The "popup" argument.
    }
    else
    {
        int mi = only_lcd ? 0 : 1;
        for (i = 1; i < match_count; ++i)
        {
            lua_pushstring(state, matches[mi++]);
            lua_rawseti(state, -2, i);
        }
    }

    // Call the filter.
    if (m_state.pcall(ondisplaymatches ? 2 : 1, 1) != 0)
        goto done;

    // Bail out if filter function didn't return a table.
    if (!lua_istable(state, -1))
        goto done;

    // Convert table returned by the Lua filter function to C.
    int j = 1;
    bool one_column = false;
    int max_visible_display = 0;
    int max_visible_description = 0;
    int new_len = int(lua_rawlen(state, -1));
    new_matches = (match_display_filter_entry**)calloc(1 + new_len + 1, sizeof(*new_matches));
    for (i = 1; i <= new_len; ++i)
    {
        lua_rawgeti(state, -1, i);
        if (!lua_isnil(state, -1))
        {
            const char* match = nullptr;
            const char* display = nullptr;
            const char* description = nullptr;
            match_type type = match_type::none;
            char append_char = 0;
            unsigned char flags = 0;

            if (lua_istable(state, -1))
            {
                lua_pushliteral(state, "match");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    match = lua_tostring(state, -1);
                lua_pop(state, 1);

                if (match)
                {
                    // Discard matches that don't match the needle.
                    int cmp = str_compare(needle, match);
                    if (cmp < 0) cmp = needle_len;
                    if (cmp < needle_len)
                        goto next;
                }

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

                lua_pushliteral(state, "appendchar");
                lua_rawget(state, -2);
                if (lua_isstring(state, -1))
                    append_char = *lua_tostring(state, -1);
                lua_pop(state, 1);

                lua_pushliteral(state, "suppressappend");
                lua_rawget(state, -2);
                if (lua_isboolean(state, -1))
                {
                    flags |= MATCH_FLAG_HAS_SUPPRESS_APPEND;
                    if (lua_toboolean(state, -1))
                        flags |= MATCH_FLAG_SUPPRESS_APPEND;
                }
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

                if (!lcd_initialized)
                {
                    lcd = match;
                    lcd_initialized = true;
                }
                else
                {
                    unsigned int matching = match ? str_compare(match, lcd.c_str()) : 0;
                    if (lcd.length() > matching)
                        lcd.truncate(matching);
                }

                size_t alloc_size = sizeof(match_display_filter_entry) + 2;
                if (match) alloc_size += strlen(match);
                if (display) alloc_size += strlen(display);
                if (description) alloc_size += strlen(description);

                match_display_filter_entry *new_match;
                new_match = (match_display_filter_entry *)malloc(alloc_size);
                memset(new_match, 0, sizeof(*new_match));
                new_match->type = (unsigned char)type;
                new_match->append_char = append_char;
                new_match->flags = flags;
                new_matches[j] = new_match;

                char* buffer = new_match->buffer;
                j++;

                // Fill in buffer with PACKED MATCH FORMAT.
                new_match->match = append_string_into_buffer(buffer, match);
                if (match && !new_match->match[0])
                {
discard:
                    free(new_match);
                    j--;
                    break;
                }

                if (!display[0])
                    goto discard;
                *(buffer++) = (char)type;   // match type
                *(buffer++) = append_char;  // append char
                *(buffer++) = flags;        // match flags
                new_match->display = append_string_into_buffer(buffer, display);
                new_match->visible_display = plainify(new_match->display, strip_markup ? &buffer : nullptr);
                if (new_match->visible_display <= 0)
                    goto discard;

                if (description)
                {
                    one_column = true;
                    new_match->description = append_string_into_buffer(buffer, description);
                    new_match->visible_description = plainify(new_match->description, strip_markup ? &buffer : nullptr);
                }
                else
                {
                    // Must append empty string even when no description,
                    // because do_popup_list expects 3 nul terminated strings.
                    // Leave new_match->description nullptr to signal there is
                    // no description (subtly different than having an empty
                    // description).
                    append_string_into_buffer(buffer, description);
                }

                if (max_visible_display < new_match->visible_display)
                    max_visible_display = new_match->visible_display;
                if (max_visible_description < new_match->visible_description)
                    max_visible_description = new_match->visible_description;
            }
            while (false);
        }

next:
        lua_pop(state, 1);
    }
    new_matches[j] = nullptr;

    // Fill in entry [0] with PACKED MATCH FORMAT (for consistency).
    // Special meaning of specific fields in the lcd [0] entry:
    //  - match is the lcd of the matches.
    //  - visible_display is the max visible_display of the entries.
    //  - visible_display negative means has descriptions (use one column).
    //  - visible_description is the max visible_description of the entries.
    //  - visible_description can be 0 when visible_display is negative; this
    //    means there are descriptions (use one column) but they are all blank.
    {
        new_matches[0] = (match_display_filter_entry*)malloc(sizeof(match_display_filter_entry) + lcd.length() + 2);
        memset(new_matches[0], 0, sizeof(*new_matches[0]));
        char* buffer = new_matches[0]->buffer;
        new_matches[0]->match = append_string_into_buffer(buffer, lcd.c_str());
        *(buffer++) = 0;    // match type
        *(buffer++) = 0;    // append char
        *(buffer++) = 0;    // match flags
        new_matches[0]->display = append_string_into_buffer(buffer, nullptr);
        append_string_into_buffer(buffer, nullptr); // Be consistent and add empty description field even in the lcd entry.

        if (one_column)
            new_matches[0]->visible_display = 0 - max_visible_display;
        else
            new_matches[0]->visible_display = max_visible_display;
        new_matches[0]->visible_description = max_visible_description;
    }

    // Remove duplicates.
    if (true)
    {
#ifdef DEBUG
        const int debug_filter = dbg_get_env_int("DEBUG_FILTER");
#endif

        str_unordered_set seen;
        unsigned int tortoise = 1;
        unsigned int hare = 1;
        while (new_matches[hare])
        {
            const char* display = new_matches[hare]->display;
            if (!display || !*display)
                display = new_matches[hare]->match;
            if (seen.find(display) != seen.end())
            {
#ifdef DEBUG
                if (debug_filter)
                    printf("%u dupe: %s\n", hare, display);
#endif
                free(new_matches[hare]);
            }
            else
            {
#ifdef DEBUG
                if (debug_filter)
                    printf("%u->%u: %s\n", hare, tortoise, display);
#endif
                seen.insert(display);
                if (hare > tortoise)
                    new_matches[tortoise] = new_matches[hare];
                tortoise++;
            }
            hare++;
        }
        new_matches[tortoise] = nullptr;
    }

    *filtered_matches = new_matches;
    goto success;
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
        return;

    bool onfiltermatches = (!lua_isnil(state, -1) && lua_toboolean(state, -1) != false);
    if (!onfiltermatches)
        return;

    // Count matches; bail if 0.
    const bool only_lcd = matches[0] && !matches[1];
    int match_count = only_lcd ? 1 : 0;
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
    int mi = only_lcd ? 0 : 1;
    lua_createtable(state, match_count, 0);
    for (int i = 1; i <= match_count; ++i)
    {
        const char* match = matches[mi++];
        match_type type = (match_type)lookup_match_type(match);

        lua_createtable(state, 0, 2);

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
        return;

    // If nil is returned then no filtering occurred.
    if (lua_isnil(state, -1))
        return;

    // Hash the filtered matches to be kept.
    str_unordered_set keep_typeless;
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

// Filtering matches lets the filter specify colors, so ignore the type.
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
        if (keep_typeless.find(*read) == keep_typeless.end())
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
