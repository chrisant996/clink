// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "scroller.h"

//------------------------------------------------------------------------------
scroller::scroller()
: m_handle(0)
{
    m_cursor_position.X = 0;
    m_cursor_position.Y = 0;
}

//------------------------------------------------------------------------------
void scroller::begin()
{
    m_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    m_cursor_position = csbi.dwCursorPosition;
}

//------------------------------------------------------------------------------
void scroller::end()
{
    SetConsoleCursorPosition(m_handle, m_cursor_position);
    m_handle = 0;
    m_cursor_position.X = 0;
    m_cursor_position.Y = 0;
}

//------------------------------------------------------------------------------
void scroller::page_up()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SMALL_RECT* wnd = &csbi.srWindow;

    int rows_per_page = wnd->Bottom - wnd->Top - 1;
    if (rows_per_page > wnd->Top)
        rows_per_page = wnd->Top;

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Top - rows_per_page;
    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
void scroller::page_down()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SMALL_RECT* wnd = &csbi.srWindow;

    int rows_per_page = wnd->Bottom - wnd->Top - 1;

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Bottom + rows_per_page;
    if (csbi.dwCursorPosition.Y > m_cursor_position.Y)
        csbi.dwCursorPosition.Y = m_cursor_position.Y;

    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
void scroller::line_up()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SMALL_RECT* wnd = &csbi.srWindow;

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Top - 1;
    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
void scroller::line_down()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SMALL_RECT* wnd = &csbi.srWindow;

    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = wnd->Bottom + 1;
    if (csbi.dwCursorPosition.Y > m_cursor_position.Y)
        csbi.dwCursorPosition.Y = m_cursor_position.Y;

    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}



//------------------------------------------------------------------------------
void scroller_module::bind_input(binder& binder)
{
    m_bind_group = binder.create_group("scroller");
    if (m_bind_group >= 0)
    {
        int default_group = binder.get_group();
        binder.bind(default_group, "\\e[5;2~", bind_id_start);

        binder.bind(m_bind_group, "\\e[5;2~", bind_id_pgup);
        binder.bind(m_bind_group, "\\e[6;2~", bind_id_pgdown);
        binder.bind(m_bind_group, "", bind_id_catchall);

        binder.bind(m_bind_group, "\\e[1;3A", bind_id_lineup);
        binder.bind(m_bind_group, "\\e[1;3B", bind_id_linedown);
        binder.bind(m_bind_group, "\\e[5;3~", bind_id_pgup);
        binder.bind(m_bind_group, "\\e[6;3~", bind_id_pgdown);
    }
}

//------------------------------------------------------------------------------
void scroller_module::on_begin_line(const context& context)
{
}

//------------------------------------------------------------------------------
void scroller_module::on_end_line()
{
}

//------------------------------------------------------------------------------
void scroller_module::on_matches_changed(const context& context)
{
}

//------------------------------------------------------------------------------
void scroller_module::on_input(
    const input& input,
    result& result,
    const context& context)
{
    switch (input.id)
    {
    case bind_id_start:
        m_scroller.begin();
        m_scroller.page_up();
        m_prev_group = result.set_bind_group(m_bind_group);
        return;

    case bind_id_pgup:
        m_scroller.page_up();
        return;

    case bind_id_pgdown:
        m_scroller.page_down();
        return;

    case bind_id_catchall:
        m_scroller.end();
        result.set_bind_group(m_prev_group);
        result.pass();
        return;

    case bind_id_lineup:
        m_scroller.line_up();
        return;

    case bind_id_linedown:
        m_scroller.line_down();
        return;
    }

}

//------------------------------------------------------------------------------
void scroller_module::on_terminal_resize(int columns, int rows, const context& context)
{
}
