// Copyright (c) 2020 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "pager_impl.h"
#include "binder.h"
#include "editor_module.h"
#include "line_buffer.h"
#include "line_state.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str_iter.h>
#include <terminal/printer.h>

extern int32 clink_is_signaled();

setting_color g_color_interact(
    "color.interact",
    "For user-interaction prompts",
    "Used when Clink displays text or prompts such as a pager's 'More?'.",
    "bold");



//------------------------------------------------------------------------------
enum {
    bind_id_pager_page      = 30,
    bind_id_pager_halfpage,
    bind_id_pager_line,
    bind_id_pager_help,
    bind_id_pager_stop,

    bind_id_catchall = binder::id_catchall_only_printable,
};



//------------------------------------------------------------------------------
pager_impl::pager_impl(input_dispatcher& dispatcher)
    : m_dispatcher(dispatcher)
{
}

//------------------------------------------------------------------------------
void pager_impl::bind_input(binder& binder)
{
    m_pager_bind_group = binder.create_group("pager");
    binder.bind(m_pager_bind_group, "?", bind_id_pager_help);
    binder.bind(m_pager_bind_group, " ", bind_id_pager_page);
    binder.bind(m_pager_bind_group, "\t", bind_id_pager_page);
    binder.bind(m_pager_bind_group, "y", bind_id_pager_page);
    binder.bind(m_pager_bind_group, "Y", bind_id_pager_page);
    binder.bind(m_pager_bind_group, "d", bind_id_pager_halfpage);
    binder.bind(m_pager_bind_group, "D", bind_id_pager_halfpage);
    binder.bind(m_pager_bind_group, "\r", bind_id_pager_line);
    binder.bind(m_pager_bind_group, "n", bind_id_pager_stop);
    binder.bind(m_pager_bind_group, "N", bind_id_pager_stop);
    binder.bind(m_pager_bind_group, "q", bind_id_pager_stop);
    binder.bind(m_pager_bind_group, "Q", bind_id_pager_stop);
    binder.bind(m_pager_bind_group, "^C", bind_id_pager_stop); // ctrl-c
    binder.bind(m_pager_bind_group, "^D", bind_id_pager_stop); // ctrl-d
    binder.bind(m_pager_bind_group, "^[", bind_id_pager_stop); // esc

    binder.bind(m_pager_bind_group, "", bind_id_catchall);
}

//------------------------------------------------------------------------------
void pager_impl::on_begin_line(const context& context)
{
}

//------------------------------------------------------------------------------
void pager_impl::on_end_line()
{
}

//------------------------------------------------------------------------------
void pager_impl::on_input(const input& input, result& result, const context& context)
{
    switch (input.id)
    {
    case bind_id_pager_help:        context.printer.print("\r\x1b[K"); print_pager_prompt(true/*help*/); result.loop(); break;
    case bind_id_pager_page:        set_limit(context.printer, page); break;
    case bind_id_pager_halfpage:    set_limit(context.printer, half_page); break;
    case bind_id_pager_line:        set_limit(context.printer, line); break;
    case bind_id_pager_stop:        m_max = -1; break;
    case bind_id_catchall:          result.loop(); break;
    }
}

//------------------------------------------------------------------------------
void pager_impl::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
}

//------------------------------------------------------------------------------
void pager_impl::on_terminal_resize(int32 columns, int32 rows, const context& context)
{
}

//------------------------------------------------------------------------------
void pager_impl::on_signal(int32 sig)
{
}

//------------------------------------------------------------------------------
void pager_impl::start_pager(printer& printer)
{
    set_limit(printer, first_page);
}

//------------------------------------------------------------------------------
bool pager_impl::on_print_lines(printer& printer, int32 lines)
{
    if (m_max < 0)
    {
        m_max = 0;
        return false;
    }

    if (m_max == 0)
        return true;

    m_max -= lines;
    if (m_max > 0)
        return true;
    m_max = 0;

    print_pager_prompt();
    m_dispatcher.dispatch(m_pager_bind_group);

    if (clink_is_signaled())
    {
        m_max = -1;
        return false;
    }

    printer.print("\x1b[1K\r");
    return m_max >= 0;
}

//------------------------------------------------------------------------------
void pager_impl::set_limit(printer& printer, pager_amount amount)
{
    switch (amount)
    {
    case unlimited:     m_max = 0; break;
    case line:          m_max = 1; break;
    case half_page:     m_max = max<int32>(printer.get_rows() / 2, 0); break;
    case page:          m_max = max<int32>(printer.get_rows() - 2, 0); break;
    case first_page:    m_max = max<int32>(printer.get_rows() - 1, 0); break;
    }
}
