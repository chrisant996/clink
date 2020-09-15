// Copyright (c) 2020 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"

//------------------------------------------------------------------------------
class pager :
    public editor_module
{
public:
    enum pager_amount { unlimited, line, half_page, page, first_page };
                    pager(input_dispatcher& dispatcher);
    void            start_pager(const context& context, pager_amount amount = first_page);
    bool            on_print_lines(const context& context, int lines);

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_matches_changed(const context& context) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override;
    int             m_max = 0;
    int             m_pager_bind_group = -1;
    input_dispatcher& m_dispatcher;
};
