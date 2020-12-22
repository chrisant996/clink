// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host_lua.h"
#include "utils/app_context.h"

#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
bool s_force_reload_scripts = false;

//------------------------------------------------------------------------------
extern "C" int show_cursor(int visible)
{
    int was_visible = 0;

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    was_visible = (GetConsoleCursorInfo(handle, &info) && info.bVisible);

    if (!was_visible != !visible)
    {
        info.bVisible = !!visible;
        SetConsoleCursorInfo(handle, &info);
    }

    return was_visible;
}

//------------------------------------------------------------------------------
host_lua::host_lua()
: m_generator(m_state)
, m_classifier(m_state)
{
    str<280> bin_path;
    app_context::get()->get_binaries_dir(bin_path);

    str<280> exe_path;
    exe_path << bin_path << "\\" CLINK_EXE;

    lua_State* state = m_state.get_state();
    lua_pushstring(state, exe_path.c_str());
    lua_setglobal(state, "CLINK_EXE");
}

//------------------------------------------------------------------------------
host_lua::operator lua_state& ()
{
    return m_state;
}

//------------------------------------------------------------------------------
host_lua::operator match_generator& ()
{
    return m_generator;
}

//------------------------------------------------------------------------------
host_lua::operator word_classifier& ()
{
    return m_classifier;
}

//------------------------------------------------------------------------------
void host_lua::load_scripts()
{
    str<280> script_path;
    app_context::get()->get_script_path(script_path);
    load_scripts(script_path.c_str());
    m_prev_script_path = script_path.c_str();
    s_force_reload_scripts = false;
}

//------------------------------------------------------------------------------
bool host_lua::load_scripts(const char* paths)
{
    if (paths == nullptr || paths[0] == '\0')
        return false;

    str<280> token;
    str_tokeniser tokens(paths, ";");
    while (tokens.next(token))
    {
        token.trim();
        load_script(token.c_str());
    }
    return true;
}

//------------------------------------------------------------------------------
void host_lua::load_script(const char* path)
{
    str<280> buffer;
    path::join(path, "*.lua", buffer);

    globber lua_globs(buffer.c_str());
    lua_globs.directories(false);

    while (lua_globs.next(buffer))
        m_state.do_file(buffer.c_str());
}

//------------------------------------------------------------------------------
bool host_lua::is_script_path_changed() const
{
    if (s_force_reload_scripts)
        return true;

    str<280> script_path;
    app_context::get()->get_script_path(script_path);
    return !script_path.iequals(m_prev_script_path.c_str());
}

//------------------------------------------------------------------------------
bool host_lua::send_event(const char* event_name, int nargs)
{
    return m_state.send_event(event_name, nargs);
}

//------------------------------------------------------------------------------
bool host_lua::send_event_cancelable(const char* event_name, int nargs)
{
    return m_state.send_event_cancelable(event_name, nargs);
}
