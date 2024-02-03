// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "suggest.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <core/os.h>
#include <lib/line_state.h>
#include <lib/matches.h>
#include <lib/suggestions.h>
#include "lua_script_loader.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "line_states_lua.h"
#include "matches_lua.h"
#include "match_builder_lua.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
extern setting_enum g_ignore_case;
extern setting_bool g_fuzzy_accent;

//------------------------------------------------------------------------------
suggester::suggester(lua_state& lua)
: m_lua(lua)
{
}

//------------------------------------------------------------------------------
bool suggester::suggest(const line_states& lines, matches* matches, int32 generation_id)
{
    const line_state& line = lines.back();

    if (!line.get_length())
    {
nosuggest:
        set_suggestion("", 0, nullptr, 0);
        return true;
    }

    lua_State* state = m_lua.get_state();
    save_stack_top ss(state);

    // Do not allow relaxed comparison for suggestions, as it is too confusing,
    // as a result of the logic to respect original case.
    int32 scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
    str_compare_scope compare(scope, g_fuzzy_accent.get());

    // Call Lua to filter prompt
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_suggest");
    lua_rawget(state, -2);

    // If matches not supplied, then use a coroutine to generates matches on
    // demand (if matches are not accessed, they will not be generated).
    if (matches)
    {
        line_state_lua line_lua(line);
        line_lua.push(state);

        line_states_lua lines_lua(lines);
        lines_lua.push(state);

        matches_lua matches_lua(*matches); // Doesn't deref matches, so nullptr is ok.
        matches_lua.push(state);

        lua_pushnil(state);
        lua_pushinteger(state, generation_id);

        if (m_lua.pcall(state, 5, 1) != 0)
            goto nosuggest;
    }
    else
    {
        std::shared_ptr<match_builder_toolkit> toolkit = make_match_builder_toolkit(generation_id, line.get_end_word_offset());

        // These can't be bound to stack objects because they must stay valid
        // for the duration of the coroutine.
        line_state_lua::make_new(state, make_line_state_copy(line), 0);
        line_states_lua::make_new(state, lines);
        matches_lua::make_new(state, toolkit);
        match_builder_lua::make_new(state, toolkit);
        lua_pushinteger(state, generation_id);

        if (m_lua.pcall(state, 5, 1) != 0)
            goto nosuggest;
    }

    const bool cancelled = lua_isboolean(state, -1) && lua_toboolean(state, -1);
    return !cancelled;
}
