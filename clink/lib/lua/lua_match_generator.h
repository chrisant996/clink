// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches/match_system.h"

struct lua_State;

//------------------------------------------------------------------------------
class lua_match_generator
    : public match_generator
{
public:
                        lua_match_generator(lua_State* state);
    virtual             ~lua_match_generator();
    virtual bool        generate(const line_state& line, matches& out) override;

private:
    void                initialise();
    void                print_error(const char* error) const;
    void                lua_pushlinestate(const line_state& line);
    bool                load_script(const char* script);
    void                load_scripts(const char* path);
    struct lua_State*   m_state;

    friend class lua_root;
};
