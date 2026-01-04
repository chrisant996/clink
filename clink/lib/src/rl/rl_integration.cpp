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
#include <terminal/printer.h>
#include <terminal/terminal_helpers.h>
#include <terminal/wcwidth.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
extern void (*rl_fwrite_function)(FILE*, const char*, int);
extern void (*rl_fflush_function)(FILE*);
}

//------------------------------------------------------------------------------
extern editor_module::result* g_result;
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

    if (!rl_filename_completion_desired && !matches->get_force_quoting())
        rl_filename_quoting_desired = 0;

#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_MATCHES"))
    {
        printf("count = %d\n", count);
        printf("filename completion desired = %d (%s)\n", rl_filename_completion_desired, iter.is_filename_completion_desired().is_explicit() ? "explicit" : "implicit");
        printf("filename display desired = %d (%s)\n", rl_filename_display_desired, iter.is_filename_display_desired().is_explicit() ? "explicit" : "implicit");
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
static bool         s_has_override_rl_last_func = false;
static uint32       s_last_func_override_counter = 0;
static rl_command_func_t* s_override_rl_last_func = nullptr;
static str_moveable s_last_luafunc;

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
void override_rl_last_func(rl_command_func_t* func, bool force_when_null)
{
    ++s_last_func_override_counter;
    s_has_override_rl_last_func = true;
    s_override_rl_last_func = func;
    if (func || force_when_null)
    {
        rl_last_func = func;
        cua_after_command();
    }
}

//------------------------------------------------------------------------------
const char* get_last_luafunc()
{
    return s_last_luafunc.c_str();
}

//------------------------------------------------------------------------------
void* get_effective_last_func()
{
    return reinterpret_cast<void*>(s_has_override_rl_last_func ? s_override_rl_last_func : rl_last_func);
}

//------------------------------------------------------------------------------
uint32 get_last_func_override_counter()
{
    return s_last_func_override_counter;
}

//------------------------------------------------------------------------------
int32 macro_hook_func(const char* macro)
{
    bool is_luafunc = (macro && strnicmp(macro, "luafunc:", 8) == 0);

    if (is_luafunc)
    {
        str<> func_name;
        func_name = macro + 8;
        func_name.trim();

        // TODO: Ideally optimize this so that it only resets match generation if
        // the Lua function triggers completion.
        reset_generate_matches();

        HANDLE std_handles[2] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE) };
        DWORD prev_mode[2];
        static_assert(_countof(std_handles) == _countof(prev_mode), "array sizes must match");
        for (size_t i = 0; i < _countof(std_handles); ++i)
            GetConsoleMode(std_handles[i], &prev_mode[i]);

        if (!host_call_lua_rl_global_function(func_name.c_str()))
            rl_ding();

        const DWORD raw_prev_mode = prev_mode[0];
        prev_mode[0] = cleanup_console_input_mode(prev_mode[0]);
        for (size_t i = 0; i < _countof(std_handles); ++i)
            SetConsoleMode(std_handles[i], prev_mode[i]);
        if (raw_prev_mode != prev_mode[0])
            debug_show_console_mode();
    }

    cua_after_command(!is_luafunc/*force_clear*/);

    return is_luafunc;
}

//------------------------------------------------------------------------------
void last_func_hook_func(int32 dispatched)
{
    if (s_has_override_rl_last_func)
    {
        rl_last_func = s_override_rl_last_func;
        s_has_override_rl_last_func = false;
    }

    cua_after_command();
    s_last_luafunc.clear();

    if (!dispatched)
        return;

    if (s_prev_inputline.length() != rl_end || memcmp(s_prev_inputline.c_str(), rl_line_buffer, rl_end))
    {
        s_prev_inputline.clear();
        s_prev_inputline.concat(rl_line_buffer, rl_end);
        host_send_oninputlinechanged_event(s_prev_inputline.c_str());
    }

    host_send_event("onaftercommand");
    maybe_redisplay_readline();
}

//------------------------------------------------------------------------------
void apply_pending_lastfunc()
{
    if (s_has_override_rl_last_func)
    {
        rl_last_func = s_override_rl_last_func;
        s_has_override_rl_last_func = false;
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
    s_has_override_rl_last_func = false;
    s_override_rl_last_func = nullptr;
}



//------------------------------------------------------------------------------
resync_rl_cursor_pos::resync_rl_cursor_pos(printer* printer, bool use_rl_fwrite)
    : m_printer(printer ? printer : g_printer)
    , m_use_rl_fwrite(use_rl_fwrite)
#ifdef DEBUG
    , m_vpos(_rl_last_v_pos)
    , m_cpos(_rl_last_c_pos)
#endif
{
    assert(m_printer);
    if (m_printer)
    {
        int16 unused;
        if (!m_printer->get_cursor_pos(m_cursor_x, unused))
        {
            assert(false);
            m_printer = nullptr;
        }
    }
}

//------------------------------------------------------------------------------
resync_rl_cursor_pos::~resync_rl_cursor_pos()
{
    resync();
}

//------------------------------------------------------------------------------
void resync_rl_cursor_pos::clear()
{
    m_printer = nullptr;
}

//------------------------------------------------------------------------------
void resync_rl_cursor_pos::resync(bool update_rl_last_pos)
{
    if (m_printer)
    {
        if (update_rl_last_pos)
        {
            assert(m_vpos == _rl_last_v_pos);
            assert(m_cpos == _rl_last_c_pos);
            _rl_move_vert(m_vpos);
            _rl_last_c_pos = m_cpos;
        }

        str<> tmp;
        tmp.format("\x1b[%uG", m_cursor_x + 1);
        if (m_use_rl_fwrite)
        {
            rl_fwrite_function(_rl_out_stream, tmp.c_str(), tmp.length());
            rl_fflush_function(_rl_out_stream);
        }
        else
        {
            m_printer->print(tmp.c_str(), tmp.length());
        }

        clear();
    }
}
