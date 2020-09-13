// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <lib/editor_module.h>

//------------------------------------------------------------------------------
class scroller
{
public:
                    scroller();
    void            begin();
    void            end();
    void            page_up();
    void            page_down();
    void            line_up();
    void            line_down();

private:
    HANDLE          m_handle;
    COORD           m_cursor_position;
};

//------------------------------------------------------------------------------
class scroller_module
    : public editor_module
{
private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_matches_changed(const context& context) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override;
    scroller        m_scroller;
    int             m_bind_group;
    int             m_prev_group;

    enum
    {
        bind_id_start,
        bind_id_pgup,
        bind_id_pgdown,
        bind_id_catchall,
        bind_id_lineup,
        bind_id_linedown,
    };
};
