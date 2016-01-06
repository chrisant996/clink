// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches/match_system.h"

#include <core/str.h>

class match_printer;
class matches;
class terminal;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
        const char*         shell_name;
        terminal*           term;
        match_printer*      match_printer;
    };

                            line_editor(const desc& desc);
                            line_editor(const line_editor&) = delete;
    void                    operator = (const line_editor&) = delete;
    virtual                 ~line_editor();
    virtual bool            edit_line(const char* prompt, str_base& out) = 0;
    terminal*               get_terminal() const;
    match_system&           get_match_system();
    match_printer*          get_match_printer() const;
    const char*             get_shell_name() const;

private:
    terminal*               m_terminal;
    match_system            m_match_system;
    match_printer*          m_match_printer;
    str<32>                 m_shell_name;

private:
};

//------------------------------------------------------------------------------
inline line_editor::line_editor(const desc& desc)
: m_terminal(desc.term)
, m_match_printer(desc.match_printer)
{
    m_shell_name << desc.shell_name;
}

//------------------------------------------------------------------------------
inline line_editor::~line_editor()
{
}

//------------------------------------------------------------------------------
inline terminal* line_editor::get_terminal() const
{
    return m_terminal;
}

//------------------------------------------------------------------------------
inline match_system& line_editor::get_match_system()
{
    return m_match_system;
}

//------------------------------------------------------------------------------
inline match_printer* line_editor::get_match_printer() const
{
    return m_match_printer;
}

//------------------------------------------------------------------------------
inline const char* line_editor::get_shell_name() const
{
    return m_shell_name.c_str();
}
