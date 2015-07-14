// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor.h"

//------------------------------------------------------------------------------
class cwd_restorer
{
public:
                    cwd_restorer();
                    ~cwd_restorer();

private:
    wstr<MAX_PATH>  m_path;
};

//------------------------------------------------------------------------------
cwd_restorer::cwd_restorer()
{
    GetCurrentDirectoryW(m_path.size(), m_path.data());
}

//------------------------------------------------------------------------------
cwd_restorer::~cwd_restorer()
{
    SetCurrentDirectoryW(m_path.c_str());
}



//------------------------------------------------------------------------------
line_editor::line_editor(const desc& desc)
: m_terminal(desc.term)
, m_match_printer(desc.match_printer)
, m_match_generator(desc.match_generator)
{
    m_shell_name << desc.shell_name;
}

//------------------------------------------------------------------------------
bool line_editor::edit_line(const wchar_t* prompt, wchar_t* out, int out_count)
{
    cwd_restorer cwd;
    return edit_line_impl(prompt, out, out_count);
}
