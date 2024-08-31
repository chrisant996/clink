// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lib/hinter.h"

class lua_state;
class line_state;

//------------------------------------------------------------------------------
class lua_hinter : public hinter
{
public:
                    lua_hinter(lua_state& lua);
    void            get_hint(const line_state& line, input_hint& out) override;

private:
    lua_state&      m_lua;
};
