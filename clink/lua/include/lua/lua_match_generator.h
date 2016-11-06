// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <lib/match_generator.h>

class lua_state;

//------------------------------------------------------------------------------
class lua_match_generator
    : public match_generator
{
public:
                    lua_match_generator(lua_state& state);
    virtual         ~lua_match_generator();

private:
    virtual bool    generate(const line_state& line, match_builder& builder) override;
    virtual int     get_prefix_length(const char* start, int length) const override;
    void            initialise();
    void            print_error(const char* error) const;
    void            lua_pushlinestate(const line_state& line);
    bool            load_script(const char* script);
    void            load_scripts(const char* path);
    lua_state&      m_state;
};
