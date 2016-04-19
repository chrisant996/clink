// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

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

private:
    void            filter_prompt(const char* in, str_base& out);
    const char*     m_name;
    /* MODE4
    lua_State*      m_lua;
    line_editor*    m_line_editor;
    MODE4 */
};
