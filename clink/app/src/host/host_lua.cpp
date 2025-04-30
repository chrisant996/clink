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
#include <lib/rl_integration.h>
#include <terminal/terminal_helpers.h>

#include <vector>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
namespace host_lua_callbacks {

void before_read_stdin(lua_saved_console_mode* saved, void* stream)
{
    saved->h = 0;
    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_stream = HANDLE(_get_osfhandle(_fileno((FILE*)stream)));
    if (h_stdin && h_stdin == h_stream)
    {
        saved->h = h_stdin;
        use_host_input_mode();
        saved->cursor_visible = show_cursor(1);
    }
}

void after_read_stdin(lua_saved_console_mode* saved)
{
    if (saved->h)
    {
        show_cursor(saved->cursor_visible);
        use_clink_input_mode();
    }
}

};

static const lua_clink_callbacks g_lua_callbacks =
{
    host_lua_callbacks::before_read_stdin,
    host_lua_callbacks::after_read_stdin,
};



//------------------------------------------------------------------------------
host_lua::host_lua()
: m_generator(m_state)
, m_hinter(m_state)
, m_classifier(m_state)
, m_idle(m_state)
{
    __lua_set_clink_callbacks(&g_lua_callbacks);

    clear_macro_descriptions();

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
host_lua::operator hinter& ()
{
    return m_hinter;
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
        if (stricmp(s, "clink.lua") == 0)
            continue;

#ifdef LUA_FILTER_8DOT3_SHORT_NAMES
        // When the 8.3 short names compatibility feature is enabled in
        // Windows (which it is by default), then a file "foo.luax" gets an
        // 8.3 short name like "foo~1.lua".  Since the file actually has two
        // names and one of the ends with ".lua", that causes the file to
        // match "*.lua".
        //
        // It's simple enough to filter the results for exact matches, but
        // that leads to inconsistencies that are then even more unexpected:
        // batch scripts and other programs using *.lua will still operate on
        // foo.luax even though Clink doesn't load it, Lua scripts in Clink
        // that use *.lua will still operate on foo.luax, and so on.
        //
        // My viewpoint is that consistency is better in this case.  The
        // example raised in chrisant996/clink#502 involved what looked like
        // renaming a file to "foo.lua_" to "hide" it.  In general, that isn't
        // reliable, and renaming to something like "foo.lua.ignore" is both
        // more effective and more descriptive.
        if (stricmp(path::get_extension(s), ".lua") != 0)
            continue;
#endif

        if (m_state.do_file(buffer.c_str()))
            num_loaded++;
        else
            num_failed++;
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
void host_lua::activate_clinkprompt_module(const char* module)
{
    m_state.activate_clinkprompt_module(module);
}

//------------------------------------------------------------------------------
void host_lua::load_colortheme_in_memory(const char* theme)
{
    m_state.load_colortheme_in_memory(theme);
}

//------------------------------------------------------------------------------
bool host_lua::send_event(const char* event_name, int32 nargs)
{
    return m_state.send_event(event_name, nargs);
}

//------------------------------------------------------------------------------
bool host_lua::send_event_string_out(const char* event_name, str_base& out, int32 nargs)
{
    return m_state.send_event_string_out(event_name, out, nargs);
}

//------------------------------------------------------------------------------
bool host_lua::send_event_cancelable(const char* event_name, int32 nargs)
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
bool host_lua::send_oninputlinechanged_event(const char* line)
{
    return m_state.send_oninputlinechanged_event(line);
}

//------------------------------------------------------------------------------
bool host_lua::call_lua_rl_global_function(const char* func_name, const line_state* line)
{
    return m_state.call_lua_rl_global_function(func_name, line);
}

//------------------------------------------------------------------------------
bool host_lua::call_lua_filter_matches(char** matches, int32 completion_type, int32 filename_completion_desired)
{
    return m_generator.filter_matches(matches, char(completion_type), !!filename_completion_desired);
}

//------------------------------------------------------------------------------
bool host_lua::get_command_word(line_state& line, str_base& command_word, bool& quoted, recognition& recog, str_base& file)
{
    return m_state.get_command_word(line, command_word, quoted, recog, file);
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void host_lua::force_gc()
{
    lua_gc(m_state.get_state(), LUA_GCCOLLECT, 0);
}
#endif
