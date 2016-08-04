// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_backend.h"

//------------------------------------------------------------------------------
class classic_match_ui
    : public editor_backend
{
private:
    enum state : unsigned char
    {
        state_none,
        state_query,
        state_pager,
        state_print,
        state_print_one,
        state_print_page,
    };

    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const char* prompt, const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_matches_changed(const context& context) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    state           begin_print(const context& context);
    state           print(const context& context, bool single_row);
    int             m_longest = 0;
    int             m_row = 0;
    int             m_prompt_bind_group = -1;
    int             m_pager_bind_group = -1;
    int             m_prev_group = -1;
    bool            m_waiting = false;
};
