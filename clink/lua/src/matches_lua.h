// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"
#include <lib/matches.h>
#include <memory>

struct lua_State;

//------------------------------------------------------------------------------
class matches_lua
    : public lua_bindable<matches_lua>
{
public:
                        matches_lua(const matches& matches);
                        matches_lua(std::shared_ptr<match_builder_toolkit>& toolkit);
                        ~matches_lua();

protected:
    int32               get_prefix(lua_State* state);
    int32               get_count(lua_State* state);
    int32               get_match(lua_State* state);
    int32               get_type(lua_State* state);
    int32               get_append_char(lua_State* state);
    int32               get_suppress_quoting(lua_State* state);

private:
    const matches*      m_matches;
    std::shared_ptr<match_builder_toolkit> m_toolkit;
    str_moveable        m_prefix;
    bool                m_has_prefix = false;

    friend class lua_bindable<matches_lua>;
    static const char* const c_name;
    static const method c_methods[];
};
