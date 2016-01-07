// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_editor;
struct lua_State;
class str_base;

//------------------------------------------------------------------------------
class host
{
public:
                    host(lua_State* lua, line_editor* editor);
    virtual         ~host();
    virtual bool    validate() = 0;
    virtual bool    initialise() = 0;
    virtual void    shutdown() = 0;
    bool            edit_line(const char* prompt, str_base& out);

private:
    lua_State*      m_lua;
    line_editor*    m_line_editor;
};
