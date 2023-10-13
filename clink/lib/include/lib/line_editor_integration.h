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

//------------------------------------------------------------------------------
bool is_showing_argmatchers();
void clear_need_collect_words();

//------------------------------------------------------------------------------
void clear_deprecated_argmatchers();
void mark_deprecated_argmatcher(const char* name);
bool has_deprecated_argmatcher(const char* name);

//------------------------------------------------------------------------------
// Suggestions are spread across host, line_editor_impl, and rl_module.  Until
// that gets cleaned up, the global functions are declared here.
void set_suggestion_started(const char* line);
void set_suggestion(const char* line, uint32 endword_offset, const char* suggestion, uint32 offset);
