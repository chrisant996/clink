// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "file_match_generator.h"

//------------------------------------------------------------------------------
class lua_match_generator
    : public file_match_generator
{
public:
                        lua_match_generator();
    virtual             ~lua_match_generator();
    virtual void        generate(const line_state& line, matches& result) override;

private:
    void                initialise(struct lua_State* state);
    void                shutdown();
    void                print_error(const char* error) const;
    void                lua_pushlinestate(const line_state& line);
    bool                load_script(const char* script);
    void                load_scripts(const char* path);
    struct lua_State*   m_state;

    friend class lua_root;
};
