// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "matches_impl.h"

#include <core/str.h>
#include <core/singleton.h>

class terminal_in;
class line_buffer;

extern line_buffer& buffer;

//------------------------------------------------------------------------------
class rl_module
    : public editor_module
    , public singleton<rl_module>
{
public:
                    rl_module(const char* shell_name, terminal_in* input, const char* state_dir);
                    ~rl_module();

    bool            is_bound(const char* seq, int len);
    bool            translate(const char* seq, int len, str_base& out);
    void            set_keyseq_len(int len);
    void            set_prompt(const char* prompt, const char* rprompt, bool redisplay);

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override;
    void            done(const char* line);
    char*           m_rl_buffer;
    int             m_prev_group;
    int             m_catch_group;
    bool            m_done;
    bool            m_eof;
    str_moveable    m_rl_prompt;
    str_moveable    m_rl_rprompt;
    str<16>         m_input_color;
    str<16>         m_selection_color;
    str<16>         m_modmark_color;
    str<16>         m_horizscroll_color;
    str<16>         m_message_color;
    str<16>         m_pager_color;
    str<16>         m_hidden_color;
    str<16>         m_readonly_color;
    str<16>         m_command_color;
    str<16>         m_alias_color;
    str<16>         m_filtered_color;
    str<16>         m_selected_color;
    str<16>         m_arg_color;
    str<16>         m_argmatcher_color;
    str<16>         m_flag_color;
    str<16>         m_none_color;
#if 0
    int             m_insert_next_len = 0;
#endif
};
