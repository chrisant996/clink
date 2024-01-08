// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>

#include <vector>

class line_editor_impl;
class host_callbacks;
class matches;
class line_buffer;
struct word;
class line_state;
class line_states;
struct host_context;
enum class display_filter_flags;
enum class collect_words_mode;

//------------------------------------------------------------------------------
void set_active_line_editor(line_editor_impl* editor, host_callbacks* callbacks);

//------------------------------------------------------------------------------
void set_prompt(const char* prompt, const char* rprompt, bool redisplay, bool transient=false);

//------------------------------------------------------------------------------
void force_update_internal(bool restrict=false);

//------------------------------------------------------------------------------
bool is_regen_blocked();
void reset_generate_matches();
void update_matches();
void reselect_matches();
matches* maybe_regenerate_matches(const char* needle, display_filter_flags flags);
matches* get_mutable_matches(bool nosort=false);

//------------------------------------------------------------------------------
uint32 collect_words(const line_buffer& buffer, std::vector<word>& words, collect_words_mode mode);
void refresh_recognizer();

//------------------------------------------------------------------------------
void host_send_event(const char* event_name);
void host_send_oninputlinechanged_event(const char* line);
bool host_call_lua_rl_global_function(const char* func_name);
bool host_call_lua_rl_global_function(const char* func_name, const line_state* line);
void host_filter_prompt();
extern "C" void host_filter_transient_prompt(int32 crlf);
int32 host_filter_matches(char** matches);
void host_invalidate_matches();
const char** host_copy_dir_history(int32* total);
void host_get_app_context(int32& id, host_context& context);

//------------------------------------------------------------------------------
void clear_deprecated_argmatchers();
void mark_deprecated_argmatcher(const char* name);
bool has_deprecated_argmatcher(const char* name);

//------------------------------------------------------------------------------
bool host_can_suggest(const line_state& line);
bool host_suggest(const line_states& lines, matches* matches, int32 generation_id);
void set_suggestion_started(const char* line);
void set_suggestion(const char* line, uint32 endword_offset, const char* suggestion, uint32 offset);
