// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "core/str.h"

class match_generator;
class match_printer;
class terminal;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
        const char*         shell_name;
        terminal*           term;
        match_generator*    match_generator;
        match_printer*      match_printer;
    };

                            line_editor(const desc& desc);
    virtual                 ~line_editor() = 0 {}
    bool                    edit_line(const char* prompt, str_base& out);
    terminal*               get_terminal() const;
    match_printer*          get_match_printer() const;
    match_generator*        get_match_generator() const;
    const char*             get_shell_name() const;

private:
    virtual bool            edit_line_impl(const char* prompt, str_base& out) = 0;
    terminal*               m_terminal;
    match_printer*          m_match_printer;
    match_generator*        m_match_generator;
    str<32>                 m_shell_name;

private:
                            line_editor(const line_editor&);    // unimplemented
    void                    operator = (const line_editor&);    // unimplemented
};

//------------------------------------------------------------------------------
inline terminal* line_editor::get_terminal() const
{
    return m_terminal;
}

//------------------------------------------------------------------------------
inline match_printer* line_editor::get_match_printer() const
{
    return m_match_printer;
}

//------------------------------------------------------------------------------
inline match_generator* line_editor::get_match_generator() const
{
    return m_match_generator;
}

//------------------------------------------------------------------------------
inline const char* line_editor::get_shell_name() const
{
    return m_shell_name.c_str();
}
