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
#include "lua_script_loader.h"
#include "lua_state.h"
#include "line_state_lua.h"
#include "matches_lua.h"

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
void suggester::suggest(line_state& line, matches& matches, str_base& out, unsigned int& offset)
{
    if (!*line.get_line())
    {
        out.clear();
        offset = 0;
        return;
    }

    lua_State* state = m_lua.get_state();

    int top = lua_gettop(state);

    // Do not allow relaxed comparison for suggestions, as it is too confusing,
    // as a result of the logic to respect original case.
    int scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
    str_compare_scope compare(scope, g_fuzzy_accent.get());

    // Call Lua to filter prompt
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_suggest");
    lua_rawget(state, -2);

    line_state_lua line_lua(line);
    line_lua.push(state);

    matches_lua matches_lua(matches);
    matches_lua.push(state);

    if (m_lua.pcall(state, 2, 2) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            m_lua.print_error(error);
        lua_pop(state, 2);
        return;
    }

    // Collect the suggestion.
    bool isnum;
    const char* suggestion = lua_tostring(state, -2);
    int start = optinteger(state, -1, 0, &isnum) - 1;
    const int line_len = int(strlen(line.get_line()));
    if (!isnum || start < 0 || start > line_len)
        start = line_len;

    out = suggestion;
    offset = start;

    lua_settop(state, top);
}
