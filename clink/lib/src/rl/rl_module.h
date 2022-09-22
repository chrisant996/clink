// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "matches_impl.h"

#include <core/str.h>
#include <core/singleton.h>

class terminal_in;
class line_buffer;
enum class mouse_input_type : unsigned char;
typedef void (__cdecl sig_func_t)(int);

extern line_buffer& buffer;

//------------------------------------------------------------------------------
class mouse_info
{
public:
    void            clear();
    int             on_click(unsigned int x, unsigned int y, bool dblclk);
    int             clicked() const;
    void            set_anchor(int anchor1, int anchor2);
    bool            get_anchor(int point, int& anchor, int& pos) const;
private:
    unsigned short  m_x;
    unsigned short  m_y;
    unsigned int    m_tick;
    unsigned char   m_clicks;
    int             m_anchor1;
    int             m_anchor2;
};

//------------------------------------------------------------------------------
class rl_module
    : public editor_module
    , public singleton<rl_module>
{
public:
                    rl_module(terminal_in* input);
                    ~rl_module();

    bool            is_bound(const char* seq, int len);
    bool            accepts_mouse_input(mouse_input_type type);
    bool            translate(const char* seq, int len, str_base& out);
    void            set_keyseq_len(int len);
    void            set_prompt(const char* prompt, const char* rprompt, bool redisplay);

    bool            is_input_pending();
    bool            next_line(str_base& out);

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override;
    virtual void    on_signal(int sig) override;
    void            done(const char* line);
    int             m_prev_group;
    int             m_catch_group;
    bool            m_done;
    bool            m_eof;
    mouse_info      m_mouse;
    std::vector<str_moveable> m_queued_lines;
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
    str<16>         m_description_color;
    str<16>         m_filtered_color;
    str<16>         m_arginfo_color;
    str<16>         m_selected_color;
    str<16>         m_suggestion_color;
    str<16>         m_arg_color;
    str<16>         m_argmatcher_color;
    str<16>         m_flag_color;
    str<16>         m_unrecognized_color;
    str<16>         m_executable_color;
    str<16>         m_none_color;
    sig_func_t*     m_old_int;
    sig_func_t*     m_old_break;
};
