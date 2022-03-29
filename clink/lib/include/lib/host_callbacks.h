// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class line_state;
class line_states;
class matches;
enum class recognition : char;

//------------------------------------------------------------------------------
class host_callbacks
{
public:
    virtual int add_history(const char* line) = 0;
    virtual int remove_history(int rl_history_index, const char* line) = 0;
    virtual void filter_prompt() = 0;
    virtual void filter_transient_prompt(bool final) = 0;
    virtual bool can_suggest(const line_state& line) = 0;
    virtual bool suggest(const line_states& lines, matches* matches, int generation_id) = 0;
    virtual void filter_matches(char** matches) = 0;
    virtual bool call_lua_rl_global_function(const char* func_name, line_state* line) = 0;
    virtual const char** copy_dir_history(int* total) = 0;
    virtual void send_event(const char* event_name) = 0;
    virtual void send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file) = 0;
    virtual bool has_event_handler(const char* event_name) = 0;
    virtual void get_app_context(int& id, str_base& binaries, str_base& profile, str_base& scripts) = 0;
};
