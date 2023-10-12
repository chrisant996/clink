// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>

class matches;
class matches_iter;
typedef int rl_command_func_t (int, int);

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
void    set_pending_luafunc(const char* macro);
const char* get_last_luafunc();
void    override_rl_last_func(rl_command_func_t* func, bool force_when_null=false);
int32   macro_hook_func(const char* macro);
void    clear_macro_descriptions();
