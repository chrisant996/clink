// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

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

    virtual void    bind(binder& binder) override;
    virtual void    begin_line(const char* prompt, const context& context) override;
    virtual void    end_line() override;
    virtual void    on_matches_changed(const context& context) override;
    virtual result  on_input(const char* keys, int id, const context& context) override;
    state           begin_print(const context& context);
    state           print(const context& context, bool single_row);
    state           query_prompt(unsigned char key, const context& context);
    state           pager_prompt(unsigned char key, const context& context);
    bool            m_waiting = false;
    int             m_longest = 0;
    int             m_row = 0;
};
