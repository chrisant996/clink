// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_integration.h"
#include "rl_commands.h"
#include "editor_module.h"
#include "line_editor_integration.h"
#include "display_readline.h"
#include "matches.h"

#include <core/base.h>
#include <core/debugheap.h>
#include <terminal/ecma48_iter.h>
#include <terminal/scroll.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/wcwidth.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
extern void (*rl_fwrite_function)(FILE*, const char*, int);
extern void (*rl_fflush_function)(FILE*);
}

#include <tib.h>
#include <tib_glue.hpp>

//------------------------------------------------------------------------------
extern editor_module::result* g_result;
std::shared_ptr<tib::input_box> g_tib;
str_moveable g_prompt_prefix;
str_moveable g_prompt;
str_moveable g_rprompt;
static bool s_force_reload_scripts = false;

//------------------------------------------------------------------------------
bool is_force_reload_scripts()
{
    return s_force_reload_scripts;
}

//------------------------------------------------------------------------------
void clear_force_reload_scripts()
{
    s_force_reload_scripts = false;
}

//------------------------------------------------------------------------------
int32 force_reload_scripts()
{
    s_force_reload_scripts = true;
    if (g_result)
        g_result->done(true); // Force a new edit line so scripts can be reloaded.
    return 0;
}



//------------------------------------------------------------------------------
void update_rl_modes_from_matches(const matches* matches, const matches_iter& iter, int32 count)
{
    switch (matches->get_suppress_quoting())
    {
    case 1: rl_filename_quoting_desired = 0; break;
    case 2: rl_completion_suppress_quote = 1; break;
    }

    rl_completion_suppress_append = matches->is_suppress_append();
    if (matches->get_append_character())
        rl_completion_append_character = matches->get_append_character();

    rl_filename_completion_desired = iter.is_filename_completion_desired();
    rl_filename_display_desired = iter.is_filename_display_desired();

    rl_command_word_completion = matches->is_command_word();

    if (!rl_filename_completion_desired && !matches->get_force_quoting())
        rl_filename_quoting_desired = 0;

#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_MATCHES"))
    {
        printf("count = %d\n", count);
        printf("filename completion desired = %d (%s)\n", rl_filename_completion_desired, iter.is_filename_completion_desired().is_explicit() ? "explicit" : "implicit");
        printf("filename display desired = %d (%s)\n", rl_filename_display_desired, iter.is_filename_display_desired().is_explicit() ? "explicit" : "implicit");
        printf("command word = %d\n", rl_command_word_completion);
        printf("get word break position = %d\n", matches->get_word_break_position());
        printf("is suppress append = %d\n", matches->is_suppress_append());
        printf("get append character = %u\n", uint8(matches->get_append_character()));
        printf("get suppress quoting = %d\n", matches->get_suppress_quoting());
        printf("get force quoting = %d\n", matches->get_force_quoting());
    }
#endif
}



//------------------------------------------------------------------------------
static str_moveable s_prev_inputline;
static str_moveable s_pending_luafunc;
static bool         s_has_pending_luafunc = false;
static bool         s_has_override_last_command = false;
static uint32       s_last_func_override_counter = 0;
static str_moveable s_override_last_command;
static str_moveable s_last_luafunc;
static bool         s_ignore_last_command_hook = false;

//------------------------------------------------------------------------------
void set_prev_inputline(const char* line, uint32 length)
{
    if (line)
    {
        s_prev_inputline.clear();
        s_prev_inputline.concat(line, length);
    }
    else
    {
        s_prev_inputline.free();
    }
}

//------------------------------------------------------------------------------
void set_pending_luafunc(const char* macro)
{
    dbg_ignore_scope(snapshot, "s_pending_luafunc");
    s_has_pending_luafunc = true;
    s_pending_luafunc.copy(macro);
}

//------------------------------------------------------------------------------
void override_last_command(const char* name, bool force_when_null)
{
    ++s_last_func_override_counter;
    s_has_override_last_command = true;
    s_override_last_command = name;
    if (name || force_when_null)
    {
        rollback<bool> rb_ignore(s_ignore_last_command_hook, true);
        g_tib->set_last_command(name);
#ifdef TIB_TODO
        // TODO-TIB: seems unnecessary anymore.
        cua_after_command();
#endif
    }
}

//------------------------------------------------------------------------------
const char* get_last_luafunc()
{
    return s_last_luafunc.c_str();
}

//------------------------------------------------------------------------------
const char* get_effective_last_command()
{
    if (s_has_override_last_command)
        return s_override_last_command.c_str();
    if (g_tib)
        return g_tib->get_last_command();
    return nullptr;
}

//------------------------------------------------------------------------------
uint32 get_last_func_override_counter()
{
    return s_last_func_override_counter;
}

//------------------------------------------------------------------------------
bool is_luafunc_command(const char* name, str_base* out)
{
    const bool is_luafunc = (name && strnicmp(name, "luafunc:", 8) == 0);

    if (!is_luafunc)
        return false;

    if (out)
    {
        out->copy(name + 8);
        out->trim();
    }

    return true;
}

//------------------------------------------------------------------------------
bool luafunc_hook_func(const char* name)
{
    str<> func_name;
    if (!is_luafunc_command(name, &func_name))
        return false;

    static bool s_busy = false;
    assert(!s_busy);
    if (s_busy)
        return true;
    rollback<bool> rb_busy(s_busy, true);

    // TODO: Ideally optimize this so that it only resets match generation if
    // the Lua function triggers completion.
    reset_generate_matches();

    HANDLE std_handles[2] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE) };
    DWORD prev_mode[2];
    static_assert(_countof(std_handles) == _countof(prev_mode), "array sizes must match");
    for (size_t i = 0; i < _countof(std_handles); ++i)
        GetConsoleMode(std_handles[i], &prev_mode[i]);

    if (!host_call_lua_rl_global_function(func_name.c_str()))
        tib::ding();

    const DWORD raw_prev_mode = prev_mode[0];
    prev_mode[0] = cleanup_console_input_mode(prev_mode[0]);
    for (size_t i = 0; i < _countof(std_handles); ++i)
        SetConsoleMode(std_handles[i], prev_mode[i]);
    if (raw_prev_mode != prev_mode[0])
        debug_show_console_mode();

#ifdef TIB_TODO
    // TODO-TIB: seems unnecessary anymore.
    cua_after_command(!is_luafunc/*force_clear*/);
#endif

    return true;
}

//------------------------------------------------------------------------------
void last_command_hook_func(int32 dispatched)
{
    if (s_ignore_last_command_hook)
        return;
    rollback<bool> rb_ignore(s_ignore_last_command_hook, true);

// TODO-TIB: the shape of this integration may need to change?
    if (s_has_override_last_command)
    {
        g_tib->set_last_command(s_override_last_command.c_str());
        s_has_override_last_command = false;
    }

    s_last_luafunc.clear();

    if (!dispatched)
        return;

    const tib::cstring input_line = g_tib->get_text();
    const tib::textpos_t end = input_line.length();
    if (s_prev_inputline.length() != end || memcmp(s_prev_inputline.c_str(), input_line.c_str(), end))
    {
        s_prev_inputline.clear();
        s_prev_inputline.concat(input_line.c_str(), end);
        host_send_oninputlinechanged_event(s_prev_inputline.c_str());
    }

    host_send_event("onaftercommand");
    display_readline();
}

//------------------------------------------------------------------------------
void apply_pending_lastfunc()
{
    if (s_has_override_last_command)
    {
        assert(g_tib);
        if (g_tib)
        {
            rollback<bool> rb_ignore(s_ignore_last_command_hook, true);
            g_tib->set_last_command(s_override_last_command.c_str());
        }
        s_has_override_last_command = false;
    }
    if (s_has_pending_luafunc)
    {
        s_last_luafunc = std::move(s_pending_luafunc);
        s_has_pending_luafunc = false;
    }
}

//------------------------------------------------------------------------------
void clear_pending_lastfunc()
{
    s_pending_luafunc.clear();
    s_has_override_last_command = false;
    s_override_last_command.clear();
}



//------------------------------------------------------------------------------
bool rl_has_queued_input()
{
#ifdef TIB_TODO
    assertimplies(rl_pending_input, RL_ISSTATE(RL_STATE_INPUTPENDING));
    assertimplies(_rl_peek_macro_key(), RL_ISSTATE(RL_STATE_MACROINPUT));
    return ((RL_ISSTATE(RL_STATE_INPUTPENDING)) ||
            (RL_ISSTATE(RL_STATE_MACROINPUT) && _rl_peek_macro_key()) ||
            _rl_pushed_input_available());
#else
    return tib::term_in_avail(0);
#endif
}



//------------------------------------------------------------------------------
resync_rl_cursor_pos::resync_rl_cursor_pos()
    : m_resync(!!g_terminal)
    , m_vpos(get_relative_cursor_row())
    , m_cpos(get_relative_cursor_column())
{
    assert(g_tib);
    assert(g_terminal);
    if (m_resync)
        m_cursor_x = g_tib->get_origin().x + m_cpos;
}

//------------------------------------------------------------------------------
resync_rl_cursor_pos::~resync_rl_cursor_pos()
{
    resync();
}

//------------------------------------------------------------------------------
void resync_rl_cursor_pos::clear()
{
    m_resync = false;
}

//------------------------------------------------------------------------------
void resync_rl_cursor_pos::resync(bool update_rl_last_pos)
{
    if (m_resync)
    {
        if (update_rl_last_pos)
            move_to_caret_position(true/*force_column*/);
        else
            clink_write(tib::term_col(m_cursor_x));

        clear();
    }
}



//------------------------------------------------------------------------------
static bool s_refilter_deferred = false;

//------------------------------------------------------------------------------
bool is_terminal_scrolled()
{
    // Temporary code to allow disabling this if it causes a problem...
    {
        str<> value;
        if (os::get_env("CLINK_NO_DEFER_REFILTER", value) && atoi(value.c_str()) != 0)
            return false;;
    }

    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    deduce_scroll_mode(hout);
    if (!is_scroll_mode())
        return false;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hout, &csbi))
        return false;

    // Consider the terminal to be scrolled if the prompt isn't fully visible
    // or the input line isn't fully visible.
    const DWORD top_y = csbi.dwCursorPosition.Y - get_relative_cursor_row();
    const DWORD bot_y = top_y + (g_tib ? g_tib->get_extent().y : 0);
    return csbi.srWindow.Top > top_y || csbi.srWindow.Bottom < bot_y;
}

//------------------------------------------------------------------------------
bool is_refilter_deferred()
{
    return s_refilter_deferred;
}

//------------------------------------------------------------------------------
bool defer_refilter(bool defer)
{
    s_refilter_deferred = defer;
    return s_refilter_deferred;
}
