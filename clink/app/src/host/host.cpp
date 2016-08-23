// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"
#include "host_backend.h"
#include "prompt.h"
#include "rl/rl_history.h"
#include "utils/app_context.h"
#include "utils/scroller.h"

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

    // Load Clink's settings.
    str<288> settings_file;
    app_context::get()->get_settings_path(settings_file);
    settings::load(settings_file.c_str());

    // Set up the string comparison mode.
    int cmp_mode;
    switch (g_ignore_case.get())
    {
    case 1:     cmp_mode = str_compare_scope::caseless; break;
    case 2:     cmp_mode = str_compare_scope::relaxed;  break;
    default:    cmp_mode = str_compare_scope::exact;    break;
    }
    str_compare_scope compare(cmp_mode);

    rl_history history;

    // Set up Lua and load scripts into it.
    lua_state lua;
    lua_match_generator lua_generator(lua);
    prompt_filter prompt_filter(lua);
/* MODE4
    lua_load_script(lua, app, git);
    lua_load_script(lua, app, go);
    lua_load_script(lua, app, hg);
    lua_load_script(lua, app, p4);
    lua_load_script(lua, app, svn);
MODE4 */
    lua_load_script(lua, app, dir);
    lua_load_script(lua, app, exec);
    lua_load_script(lua, app, self);
    initialise_lua(lua);
    load_lua_scripts(lua);

    line_editor::desc desc = {};
    initialise_editor_desc(desc);

    // Filter the prompt.
    str<256> filtered_prompt;
    prompt_filter.filter(prompt, filtered_prompt);
    desc.prompt = filtered_prompt.c_str();

    // Set the terminal that will handle all IO while editing.
    win_terminal terminal;
    desc.terminal = &terminal;

    // Create the editor and add components to it.
    line_editor* editor = line_editor_create(desc);

    editor_backend* ui = classic_match_ui_create();
    editor->add_backend(*ui);

    scroller_backend scroller;
    editor->add_backend(scroller);

    host_backend host_backend(m_name);
    editor->add_backend(host_backend);

    editor->add_generator(lua_generator);
    editor->add_generator(file_match_generator());

    bool ret = false;
    while (1)
    {
        if (ret = editor->edit(out.data(), out.size()))
        {
            if (history.expand(out.c_str(), out) == 2)
            {
                puts(out.c_str());
                continue;
            }

            history.add(out.c_str());
        }
        break;
    }

    line_editor_destroy(editor);
    classic_match_ui_destroy(ui);
    return ret;
}
