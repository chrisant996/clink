// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

class lua_state;
class str_base;
class line_states;
class matches;

//------------------------------------------------------------------------------
struct suggestion
{
    str_moveable    m_suggestion;
    uint32          m_suggestion_offset = -1;
    str_moveable    m_source;
};

//------------------------------------------------------------------------------
class suggester
{
public:
                    suggester(lua_state& lua);
    bool            suggest(const line_states& lines, matches* matches, int32 generation_id);

private:
    lua_state&      m_lua;
};
