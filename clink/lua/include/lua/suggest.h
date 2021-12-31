// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class lua_state;
class str_base;
class line_state;
class matches;

//------------------------------------------------------------------------------
class suggester
{
public:
                    suggester(lua_state& lua);
    bool            suggest(line_state& line, matches* matches, int generation_id);

private:
    lua_state&      m_lua;
};
