// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"

#include <core/os.h>
#include <core/str.h>
#include <line_editor.h>

//------------------------------------------------------------------------------
struct cwd_restorer
{
                  cwd_restorer()  { os::get_current_dir(m_path); }
                  ~cwd_restorer() { os::set_current_dir(m_path.c_str()); }
    str<MAX_PATH> m_path;
};



//------------------------------------------------------------------------------
host::host(line_editor* editor)
: m_line_editor(editor)
{
}

//------------------------------------------------------------------------------
host::~host()
{
}

//------------------------------------------------------------------------------
bool host::edit_line(const char* prompt, str_base& out)
{
    cwd_restorer cwd;
    return m_line_editor->edit_line(prompt, out);
}
