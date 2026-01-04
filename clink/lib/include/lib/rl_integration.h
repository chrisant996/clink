// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>

class matches;
class matches_iter;
typedef int rl_command_func_t (int, int);
class printer;

//------------------------------------------------------------------------------
// Readline is based around global variables and global functions, which
// doesn't mesh well with object oriented design.  The following global
// functions help bridge that gap.

//------------------------------------------------------------------------------
bool    is_force_reload_scripts();
void    clear_force_reload_scripts();
int32   force_reload_scripts();

//------------------------------------------------------------------------------
void    update_rl_modes_from_matches(const matches* matches, const matches_iter& iter, int32 count);

//------------------------------------------------------------------------------
const char* get_last_prompt();

//------------------------------------------------------------------------------
void    increment_line_generation_id();

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
void    signal_terminal_resized();
void    set_refilter_after_resize(bool refilter);

//------------------------------------------------------------------------------
class resync_rl_cursor_pos
{
public:
                resync_rl_cursor_pos(printer* printer, bool use_rl_fwrite=false);
                ~resync_rl_cursor_pos();
    void        clear();
    void        resync(bool update_rl_last_pos=true);
    int16       get_cursor_x() const { return m_cursor_x; }
private:
    printer*    m_printer;
    int16       m_cursor_x;
    const bool  m_use_rl_fwrite;
#ifdef DEBUG
    const int32 m_vpos;
    const int32 m_cpos;
#endif
};
