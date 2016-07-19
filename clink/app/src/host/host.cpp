// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"
#include "rl/rl_history.h"

#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <lib/match_generator.h>
#include <lib/line_editor.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <lua/lua_match_generator.h>
#include <terminal/win_terminal.h>

//------------------------------------------------------------------------------
static setting_str g_clink_path(
    "clink.path",
    "Paths to load Lua completion scripts from",
    "These paths will be searched for Lua scripts that provide custom\n"
    "match generation. Multiple paths should be delimited by semicolons.",
    "");

static setting_enum g_ignore_case(
    "match.ignore_case",
    "Case insensitive matching",
    "Toggles whether case is ignored when selecting matches. The 'relaxed'\n"
    "option will also consider -/_ as equal.",
    "off,on,relaxed",
    2);



//------------------------------------------------------------------------------
void load_clink_settings();



//------------------------------------------------------------------------------
static void load_lua_script(lua_state& lua, const char* path)
{
    str<> buffer;
    path::join(path, "*.lua", buffer);

    globber lua_globs(buffer.c_str());
    lua_globs.directories(false);

    while (lua_globs.next(buffer))
        lua.do_file(buffer.c_str());
}

//------------------------------------------------------------------------------
static void load_lua_scripts(lua_state& lua, const char* paths)
{
    if (paths == nullptr || paths[0] == '\0')
        return;

    str<> token;
    str_tokeniser tokens(paths, ";");
    while (tokens.next(token))
        load_lua_script(lua, token.c_str());
}

//------------------------------------------------------------------------------
static void load_lua_scripts(lua_state& lua)
{
    const char* setting_clink_path = g_clink_path.get();
    load_lua_scripts(lua, setting_clink_path);

    str<256> env_clink_path;
    os::get_env("clink_path", env_clink_path);
    load_lua_scripts(lua, env_clink_path.c_str());
}



//------------------------------------------------------------------------------
host::host(const char* name)
: m_name(name)
{
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

    load_clink_settings();

    int cmp_mode;
    switch (g_ignore_case.get())
    {
    case 1:     cmp_mode = str_compare_scope::caseless; break;
    case 2:     cmp_mode = str_compare_scope::relaxed;  break;
    default:    cmp_mode = str_compare_scope::exact;    break;
    }

    str_compare_scope compare(cmp_mode);

    win_terminal terminal;
    rl_history history;
    editor_backend* ui = classic_match_ui_create();

    lua_state lua;
    lua_match_generator lua_generator(lua);
/* MODE4
    lua_load_script(lua, app, git);
    lua_load_script(lua, app, go);
    lua_load_script(lua, app, hg);
    lua_load_script(lua, app, p4);
    lua_load_script(lua, app, svn);
    lua_load_script(lua, app, prompt);
MODE4 */
    lua_load_script(lua, app, dir);
    lua_load_script(lua, app, exec);
    lua_load_script(lua, app, self);
    initialise_lua(lua);
    load_lua_scripts(lua);

#if MODE4
    str<128> filtered_prompt;
    filter_prompt(prompt, filtered_prompt);
#endif

    line_editor::desc desc = {};
    desc.prompt = prompt;
    desc.quote_pair = "\"";
    desc.command_delims = "&|";
    desc.word_delims = " \t<>=;";
    desc.partial_delims = "\\/:";
    desc.auto_quote_chars = " %=;&^";
    desc.terminal = &terminal;

    line_editor* editor = line_editor_create(desc);
    editor->add_backend(*ui);

    editor->add_generator(lua_generator);
    editor->add_generator(file_match_generator());

    bool ret = editor->edit(out.data(), out.size());

    if (ret)
        history.add(out.c_str());

    line_editor_destroy(editor);
    classic_match_ui_destroy(ui);

    return ret;
}

//------------------------------------------------------------------------------
void host::filter_prompt(const char* in, str_base& out)
{
#if MODE4
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
#endif // MODE4
}
