// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <lib/line_editor.h>

class lua_state;
class str_base;

//------------------------------------------------------------------------------
class host
{
public:
                    host(const char* name);
    virtual         ~host();
    virtual bool    validate() = 0;
    virtual bool    initialise() = 0;
    virtual void    shutdown() = 0;

protected:
    bool            edit_line(const char* prompt, str_base& out);
    virtual void    initialise_lua(lua_state& lua) = 0;
    virtual void    initialise_editor_desc(line_editor::desc& desc) = 0;

private:
    void            filter_prompt(const char* in, str_base& out);
    const char*     m_name;
};
