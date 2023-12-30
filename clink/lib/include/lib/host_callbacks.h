// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <list>

//------------------------------------------------------------------------------
class line_state;
class line_states;
class matches;
enum class recognition : char;

//------------------------------------------------------------------------------
struct host_context
{
    str_moveable    binaries;
    str_moveable    profile;
    str_moveable    default_settings;
    str_moveable    default_inputrc;
    str_moveable    settings;
    str_moveable    scripts;
};

//------------------------------------------------------------------------------
class host_callbacks
{
public:
    virtual void filter_prompt() = 0;
    virtual void filter_transient_prompt(bool final) = 0;
    virtual bool can_suggest(const line_state& line) = 0;
    virtual bool suggest(const line_states& lines, matches* matches, int32 generation_id) = 0;
    virtual bool filter_matches(char** matches) = 0;
    virtual bool call_lua_rl_global_function(const char* func_name, const line_state* line) = 0;
    virtual const char** copy_dir_history(int32* total) = 0;
    virtual void send_event(const char* event_name) = 0;
    virtual void send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file) = 0;
    virtual void send_oninputlinechanged_event(const char* line) = 0;
    virtual bool has_event_handler(const char* event_name) = 0;
};

//------------------------------------------------------------------------------
void host_cmd_enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line);
void host_get_app_context(int32& id, host_context& context);
