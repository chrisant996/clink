// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>

#include <tib.h>

class matches;
class matches_iter;
typedef int rl_command_func_t (int, int);
class printer;

//------------------------------------------------------------------------------
// Readline is based around global variables and global functions, which
// doesn't mesh well with object oriented design.  The following global
// functions help bridge that gap.

//------------------------------------------------------------------------------
extern std::shared_ptr<tib::input_box> g_tib;
extern str_moveable g_prompt_prefix;
extern str_moveable g_prompt;
extern str_moveable g_rprompt;

//------------------------------------------------------------------------------
#define clink_write tib::term_out

//------------------------------------------------------------------------------
bool    is_force_reload_scripts();
void    clear_force_reload_scripts();
int32   force_reload_scripts();

//------------------------------------------------------------------------------
void    update_rl_modes_from_matches(const matches* matches, const matches_iter& iter, int32 count);

//------------------------------------------------------------------------------
const char* get_last_prompt();
void init_prompt(const str_base& prompt, const str_base& rprompt);

//------------------------------------------------------------------------------
void    set_prev_inputline(const char* line, uint32 length=-1);
void    set_pending_luafunc(const char* macro);
void    override_rl_last_func(rl_command_func_t* func, bool force_when_null=false);
const char* get_last_luafunc();
void*   get_effective_last_func();
uint32  get_last_func_override_counter();
int32   macro_hook_func(const char* macro);
void    last_func_hook_func(int32 dispatched);
void    apply_pending_lastfunc();
void    clear_pending_lastfunc();

//------------------------------------------------------------------------------
void    add_macro_description(const char* macro, const char* desc);
void    clear_macro_descriptions();
bool    translate_keyseq(const char* keyseq, uint32 len, char** key_name, bool friendly, int32& sort);

//------------------------------------------------------------------------------
bool    rl_has_queued_input();

//------------------------------------------------------------------------------
bool    is_terminal_scrolled();
bool    is_refilter_deferred();
bool    defer_refilter(bool defer);

//------------------------------------------------------------------------------
void    signal_terminal_resized();
void    set_refilter_after_resize(bool refilter);

//------------------------------------------------------------------------------
class resync_rl_cursor_pos
{
public:
                resync_rl_cursor_pos();
                ~resync_rl_cursor_pos();
    void        clear();
    void        resync(bool update_rl_last_pos=true);
    int16       get_cursor_x() const { return m_cursor_x; }
private:
    bool        m_resync;
    int16       m_cursor_x;
    const int32 m_vpos;
    const int32 m_cpos;
};
