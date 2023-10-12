// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class matches;
enum class display_filter_flags;

//------------------------------------------------------------------------------
// Readline is based around global variables and global functions, which
// doesn't mesh well with the object oriented design of line_editor.  The
// following global functions help bridge that gap.

//------------------------------------------------------------------------------
void set_prompt(const char* prompt, const char* rprompt, bool redisplay);

//------------------------------------------------------------------------------
void force_update_internal(bool restrict=false);

//------------------------------------------------------------------------------
bool is_regen_blocked();
void update_matches();
void reselect_matches();
void reset_generate_matches();
matches* maybe_regenerate_matches(const char* needle, display_filter_flags flags);
matches* get_mutable_matches(bool nosort=false);
