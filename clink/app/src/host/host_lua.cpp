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
#include <core/log.h>

#include <vector>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
extern bool is_force_reload_scripts();
extern void clear_force_reload_scripts();

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
    // Load scripts.
    str<280> script_path;
    app_context::get()->get_script_path(script_path);
    load_scripts(script_path.c_str());
    m_prev_script_path = script_path.c_str();
    clear_force_reload_scripts();

    // Set the script paths so argmatchers can be loaded from completion dirs.
    {
        lua_State* state = m_state.get_state();
        save_stack_top ss(state);

        lua_getglobal(state, "clink");
        lua_pushliteral(state, "_set_completion_dirs");
        lua_rawget(state, -2);

        lua_pushlstring(state, script_path.c_str(), script_path.length());

        m_state.pcall(1, 0);
    }
}

//------------------------------------------------------------------------------
bool host_lua::load_scripts(const char* paths)
{
    if (paths == nullptr || paths[0] == '\0')
        return false;

    os::high_resolution_clock clock;
    unsigned num_loaded = 0;
    unsigned num_failed = 0;

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
            {
                if (m_state.do_file(clink.c_str()))
                    num_loaded++;
                else
                    num_failed++;
            }
        }

        // Expand and normalize the directory.
        path::tilde_expand(token.c_str(), tmp);
        path::normalise(tmp);
        path::maybe_strip_last_separator(tmp);

        // Load a given directory only once.
        transform = tmp.c_str();
        str_transform(transform.c_str(), transform.length(), out, transform_mode::lower);
        if (seen.find(out.c_str()) != seen.end())
            continue;
        seen.emplace(out.c_str());
        seen_strings.emplace_back(std::move(out));

        load_script(tmp.c_str(), num_loaded, num_failed);
    }

    if (num_failed)
        LOG("Loaded %u Lua scripts in %u ms (%u failed)", num_loaded, unsigned(clock.elapsed() * 1000), num_failed);
    else
        LOG("Loaded %u Lua scripts in %u ms", num_loaded, unsigned(clock.elapsed() * 1000));

    return true;
}

//------------------------------------------------------------------------------
void host_lua::load_script(const char* path, unsigned& num_loaded, unsigned& num_failed)
{
    str_moveable buffer;
    path::join(path, "*.lua", buffer);

    globber lua_globs(buffer.c_str());
    lua_globs.directories(false);

    while (lua_globs.next(buffer))
    {
        const char* s = path::get_name(buffer.c_str());
        if (stricmp(s, "clink.lua") != 0)
        {
            if (m_state.do_file(buffer.c_str()))
                num_loaded++;
            else
                num_failed++;
        }
    }
}

//------------------------------------------------------------------------------
bool host_lua::is_script_path_changed() const
{
    if (is_force_reload_scripts())
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
bool host_lua::send_event_string_out(const char* event_name, str_base& out, int nargs)
{
    return m_state.send_event_string_out(event_name, out, nargs);
}

//------------------------------------------------------------------------------
bool host_lua::send_event_cancelable(const char* event_name, int nargs)
{
    return m_state.send_event_cancelable(event_name, nargs);
}

//------------------------------------------------------------------------------
bool host_lua::send_event_cancelable_string_inout(const char* event_name, const char* string, str_base& out, std::list<str_moveable>* more_out)
{
    return m_state.send_event_cancelable_string_inout(event_name, string, out, more_out);
}

//------------------------------------------------------------------------------
bool host_lua::send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file)
{
    return m_state.send_oncommand_event(line, command, quoted, recog, file);
}

//------------------------------------------------------------------------------
bool host_lua::call_lua_rl_global_function(const char* func_name, line_state* line)
{
    return m_state.call_lua_rl_global_function(func_name, line);
}

//------------------------------------------------------------------------------
void host_lua::call_lua_filter_matches(char** matches, int completion_type, int filename_completion_desired)
{
    m_generator.filter_matches(matches, char(completion_type), !!filename_completion_desired);
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void host_lua::force_gc()
{
    lua_gc(m_state.get_state(), LUA_GCCOLLECT, 0);
}
#endif
