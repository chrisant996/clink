// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "doskey.h"
#include "history/history_db.h"
#include "terminal/terminal.h"

#include <lib/line_editor.h>

#include <list>

class lua_state;
class str_base;
class host_lua;
class prompt_filter;

//------------------------------------------------------------------------------
class host
{
public:
                    host(const char* name);
    virtual         ~host();
    virtual bool    validate() = 0;
    virtual bool    initialise() = 0;
    virtual void    shutdown() = 0;

    void            enqueue_lines(std::list<str_moveable>& lines);

protected:
    bool            edit_line(const char* prompt, str_base& out);
    virtual void    initialise_lua(lua_state& lua) = 0;
    virtual void    initialise_editor_desc(line_editor::desc& desc) = 0;

private:
    void            filter_prompt(const char* in, str_base& out);
    const char*     m_name;
    doskey          m_doskey;
    doskey_alias    m_doskey_alias;
    terminal        m_terminal;
    printer*        m_printer;
    history_db*     m_history = nullptr;
    host_lua*       m_lua = nullptr;
    prompt_filter*  m_prompt_filter = nullptr;
    std::list<str_moveable> m_queued_lines;
};
