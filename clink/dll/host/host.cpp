// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"

#include <core/os.h>
#include <core/str.h>
#include <line_editor.h>
#include <lua/lua_script_loader.h>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
struct cwd_restorer
{
                  cwd_restorer()  { os::get_current_dir(m_path); }
                  ~cwd_restorer() { os::set_current_dir(m_path.c_str()); }
    str<MAX_PATH> m_path;
};



//------------------------------------------------------------------------------
host::host(lua_State* lua, line_editor* editor)
: m_lua(lua)
, m_line_editor(editor)
{
    lua_load_script(lua, dll, dir);
    lua_load_script(lua, dll, env);
    lua_load_script(lua, dll, exec);
    lua_load_script(lua, dll, git);
    lua_load_script(lua, dll, go);
    lua_load_script(lua, dll, hg);
    lua_load_script(lua, dll, p4);
    lua_load_script(lua, dll, prompt);
    lua_load_script(lua, dll, self);
    lua_load_script(lua, dll, svn);
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
