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
#include <core/str_transform.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>

#include <vector>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
extern setting_bool g_adjust_cursor_style;

//------------------------------------------------------------------------------
extern "C" int show_cursor(int visible)
{
    int was_visible = 0;

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    was_visible = (GetConsoleCursorInfo(handle, &info) && info.bVisible);

    if (!g_adjust_cursor_style.get())
        return was_visible;

    if (!was_visible != !visible)
    {
        info.bVisible = !!visible;
        SetConsoleCursorInfo(handle, &info);
    }

    return was_visible;
}

//------------------------------------------------------------------------------
static DWORD s_host_input_mode = -1;
static DWORD s_clink_input_mode = -1;

//------------------------------------------------------------------------------
void save_host_input_mode(DWORD mode)
{
    s_host_input_mode = mode;
}

//------------------------------------------------------------------------------
extern "C" void use_host_input_mode()
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE)
    {
        DWORD mode;
        if (GetConsoleMode(h, &mode))
            s_clink_input_mode = mode;

        if (s_host_input_mode != -1)
            SetConsoleMode(h, s_host_input_mode);
    }
}

//------------------------------------------------------------------------------
extern "C" void use_clink_input_mode()
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE)
    {
        DWORD mode;
        if (s_host_input_mode == -1 && GetConsoleMode(h, &mode))
            s_host_input_mode = mode;

        if (s_clink_input_mode != -1)
            SetConsoleMode(h, s_clink_input_mode);
    }
}



//------------------------------------------------------------------------------
bool s_force_reload_scripts = false;

//------------------------------------------------------------------------------
host_lua::host_lua()
: m_generator(m_state)
, m_classifier(m_state)
, m_idle(m_state)
{
    str<280> bin_path;
    app_context::get()->get_binaries_dir(bin_path);

    str<280> exe_path;
    exe_path << bin_path << "\\" CLINK_EXE;

    lua_State* state = m_state.get_state();
    lua_pushlstring(state, exe_path.c_str(), exe_path.length());
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
host_lua::operator input_idle* ()
{
    return &m_idle;
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

    bool first = true;

    std::vector<wstr_moveable> seen_strings;
    wstr_unordered_set seen;
    wstr<280> transform;
    wstr_moveable out;
    str<280> tmp;

    str<280> token;
    str_tokeniser tokens(paths, ";");
    while (tokens.next(token))
    {
        token.trim();

        if (first)
        {
            // Cmder relies on being able to replace the v0.4.9 clink.lua file.
            // Clink no longer uses that file, but to accommodate Cmder Clink
            // will continue to load clink.lua from the first script path, if
            // such a file exists.
            first = false;
            str<280> clink;
            if (path::join(token.c_str(), "clink.lua", clink) &&
                os::get_path_type(clink.c_str()) == os::path_type_file)
                m_state.do_file(clink.c_str());
        }

        // Load a given directory only once.
        tmp = token.c_str();
        path::normalise(tmp);
        path::maybe_strip_last_separator(tmp);
        transform = tmp.c_str();
        str_transform(transform.c_str(), transform.length(), out, transform_mode::lower);
        if (seen.find(out.c_str()) != seen.end())
            continue;
        seen.emplace(out.c_str());
        seen_strings.emplace_back(std::move(out));

        load_script(token.c_str());
    }
    return true;
}

//------------------------------------------------------------------------------
void host_lua::load_script(const char* path)
{
    str_moveable buffer;
    path::join(path, "*.lua", buffer);

    globber lua_globs(buffer.c_str());
    lua_globs.directories(false);

#if 1
    while (lua_globs.next(buffer))
    {
        const char* s = path::get_name(buffer.c_str());
        if (stricmp(s, "clink.lua") != 0)
            m_state.do_file(buffer.c_str());
    }
#else
    // This feels dangerous...

    // Engineering compromise:  The cmder-powerline-prompt scripts use a config
    // script named "_powerline_config.lua".  In the file system, that sorts
    // AFTER the rest of the "powerline_foo.lua" files, making it impossible for
    // the configuration script to influence the other scripts until the prompt
    // is filtered.
    //
    // Ideally a better config file name would have been chosen; it's too late
    // to change its name without introducing compatibility problems.  Since
    // it's a fairly natural assumption or wish that '.' and '_' might go first,
    // Clink will actually enforce that here by deferring loading other scripts
    // until a second pass.
    //
    // This required introducing str_moveable in order to avoid copying from str
    // to std::string, since str doesn't have a move constructor (and can't
    // without causing performance problems; it has a fixed-size buffer intended
    // to boost performance by generally avoiding allocating a buffer, and
    // a move constructor would need to copy that buffer).
    std::vector<str_moveable> pass2;
    while (lua_globs.next(buffer))
    {
        const char* s = path::get_name(buffer.c_str());
        if (s && (*s == '.' || *s == '_'))
            m_state.do_file(buffer.c_str());
        else if (stricmp(s, "clink.lua") != 0)
            pass2.push_back(std::move(buffer));
    }

    for (auto const& file : pass2)
        m_state.do_file(file.c_str());
#endif
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

//------------------------------------------------------------------------------
bool host_lua::send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out)
{
    return m_state.send_event_cancelable_string_inout(event_name, string, out);
}

//------------------------------------------------------------------------------
bool host_lua::call_lua_rl_global_function(const char* func_name)
{
    return m_state.call_lua_rl_global_function(func_name);
}

//------------------------------------------------------------------------------
void host_lua::call_lua_filter_matches(char** matches, int completion_type, int filename_completion_desired)
{
    m_generator.filter_matches(matches, char(completion_type), !!filename_completion_desired);
}
