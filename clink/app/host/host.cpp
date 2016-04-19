// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"
#include "rl/rl_backend.h"

#include <core/os.h>
#include <core/str.h>
#include <line_editor.h>
#include <lib/match_generator.h>
#include <lua/lua_script_loader.h>
#include <terminal/win_terminal.h>

/* MODE4
extern "C" {
#include <lua.h>
}
MODE4 */

//------------------------------------------------------------------------------
host::host(const char* name)
: m_name(name)
/* MODE4
: m_lua(lua)
, m_line_editor(editor)
MODE4 */
{
/* MODE
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
MODE4 */
}

//------------------------------------------------------------------------------
host::~host()
{
}

//------------------------------------------------------------------------------
bool host::edit_line(const char* prompt, str_base& out)
{
    struct cwd_restorer
    {
        cwd_restorer()  { os::get_current_dir(m_path); }
        ~cwd_restorer() { os::set_current_dir(m_path.c_str()); }
        str<288>        m_path;
    } cwd;

    static rl_backend backend(m_name);
    win_terminal terminal;

    line_editor::desc desc = {};
    desc.prompt = prompt;
    desc.quote_pair = "\"";
    desc.word_delims = " \t=";
    desc.partial_delims = "\\/:";
    desc.terminal = &terminal;
    desc.backend = &backend;
    desc.buffer = &backend;
    line_editor editor(desc);
    // MODE4 editor.add_backend(ui);
    editor.add_generator(file_match_generator());

    return editor.edit(out.data(), out.size());

/* MODE4
    str<128> filtered_prompt;
    filter_prompt(prompt, filtered_prompt);

    terminal* term = m_line_editor->get_terminal();
    term->begin();
    bool ret = m_line_editor->edit_line(filtered_prompt.c_str(), out);
    term->end();

    return ret;
MODE4 */

    return true;
}

//------------------------------------------------------------------------------
void host::filter_prompt(const char* in, str_base& out)
{
/* MODE4
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
MODE4 */
}
