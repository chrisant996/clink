// Copyright (c) 2020 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"
#include "pager.h"

//------------------------------------------------------------------------------
class pager_impl
    : public editor_module
    , public pager
{
public:
    enum pager_amount { unlimited, line, half_page, page, first_page };
                    pager_impl(input_dispatcher& dispatcher);
    virtual void    start_pager(printer& printer) override;
    virtual bool    on_print_lines(printer& printer, int32 lines) override;

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_need_input(int32& bind_group) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int32 columns, int32 rows, const context& context) override;
    virtual void    on_signal(int32 sig) override;
    void            set_limit(printer& printer, pager_amount amount);
    int32           m_max = 0;
    int32           m_pager_bind_group = -1;
    input_dispatcher& m_dispatcher;
};
