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
    void            suggest(line_state& line, matches& matches, str_base& out, str_base& out_line);

private:
    lua_state&      m_lua;
};
