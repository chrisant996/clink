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

    str<128> filtered_prompt;
    filter_prompt(prompt, filtered_prompt);

    return m_line_editor->edit_line(filtered_prompt.c_str(), out);
}

//------------------------------------------------------------------------------
void host::filter_prompt(const char* in, str_base& out)
{
    // Call Lua to filter prompt
    lua_getglobal(m_lua, "clink");
    lua_pushliteral(m_lua, "filter_prompt");
    lua_rawget(m_lua, -2);

    lua_pushstring(m_lua, in);
    if (lua_pcall(m_lua, 1, 1, 0) != 0)
    {
        puts(lua_tostring(m_lua, -1));
        lua_pop(m_lua, 2);
        return;
    }

    // Collect the filtered prompt.
    const char* prompt = lua_tostring(m_lua, -1);
    out = prompt;

    lua_pop(m_lua, 2);
}
