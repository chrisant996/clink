// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "suggestionlist_impl.h"
#include "selectcomplete_impl.h" // For is_select_complete_active().
#include "binder.h"
#include "editor_module.h"
#include "line_buffer.h"
#include "line_state.h"
#include "display_readline.h"
#include "ellipsify.h"
#include "line_editor_integration.h"
#include "rl_integration.h"
#include "suggestions.h"
#ifdef SHOW_VERT_SCROLLBARS
#include "scroll_car.h"
#endif

#include <core/base.h>
#include <core/settings.h>
#include <core/str_compare.h>
#include <core/str_iter.h>
#include <rl/rl_commands.h>
#include <terminal/printer.h>
#include <terminal/ecma48_iter.h>
#include <terminal/key_tester.h>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
#include <readline/colors.h>
extern int _rl_last_v_pos;
};



//------------------------------------------------------------------------------
enum {
    bind_id_suggestionlist_up = 60,
    bind_id_suggestionlist_down,
    bind_id_suggestionlist_pgup,
    bind_id_suggestionlist_pgdn,
    bind_id_suggestionlist_leftclick,
    bind_id_suggestionlist_doubleclick,
    bind_id_suggestionlist_wheelup,
    bind_id_suggestionlist_wheeldown,
    bind_id_suggestionlist_drag,
    bind_id_suggestionlist_enter,
    bind_id_suggestionlist_escape,

    bind_id_suggestionlist_catchall,
};

const int32 c_max_suggestion_rows = 10;
const int32 c_max_listview_width = 100;



//------------------------------------------------------------------------------
static bool s_enable_suggestion_list = false;
static suggestionlist_impl* s_suggestionlist = nullptr;

//------------------------------------------------------------------------------
suggestionlist_impl::suggestionlist_impl(input_dispatcher& dispatcher)
    : m_dispatcher(dispatcher)
{
}

//------------------------------------------------------------------------------
void suggestionlist_impl::enable(bool enable)
{
    const bool was_active = is_active();

    m_hide = !enable;

    if (was_active != is_active())
    {
        if (enable)
            update_layout();
        update_display();
    }
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::activate(editor_module::result& result, bool reactivate)
{
    assert(m_buffer);
    if (!m_buffer)
        return false;

#if 0
    if (reactivate && m_point >= 0 && m_len >= 0 && m_point + m_len <= m_buffer->get_length() && m_inserted)
    {
#ifdef DEBUG
        rollback<int32> rb(m_prev_bind_group, 999999); // Dummy to make assertion happy in insert_needle().
#endif
        insert_needle();
    }
#endif

    m_applied = false;

    init_suggestions();

    if (!m_count)
    {
cant_activate:
        m_index = -1;
        return false;
    }

    if (!reactivate)
    {
        assert(!m_any_displayed);
        assert(!m_clear_display);
        m_any_displayed = false;
        m_clear_display = false;
    }

    // Make sure there's room.
    update_layout();
    if (m_visible_rows <= 0)
        goto cant_activate;

    // Disable the comment row.
    g_display_manager_no_comment_row = true;

    // Activate key bindings.
    assert(m_prev_bind_group < 0);
    m_prev_bind_group = result.set_bind_group(m_bind_group);

    // Update the display.
    reset_top();
    update_display();

    return true;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::point_within(int32 in) const
{
    // return is_active() && m_point >= 0 && in >= m_point && in < m_point + m_len;
    return false;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::bind_input(binder& binder)
{
    const char* esc = get_bindable_esc();

    m_bind_group = binder.create_group("selectcomplete");
    binder.bind(m_bind_group, "\\e[A", bind_id_suggestionlist_up);
    binder.bind(m_bind_group, "\\e[B", bind_id_suggestionlist_down);
    binder.bind(m_bind_group, "\\e[5~", bind_id_suggestionlist_pgup);
    binder.bind(m_bind_group, "\\e[6~", bind_id_suggestionlist_pgdn);
    binder.bind(m_bind_group, "\\e[$*;*L", bind_id_suggestionlist_leftclick, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*;*D", bind_id_suggestionlist_doubleclick, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*A", bind_id_suggestionlist_wheelup, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*B", bind_id_suggestionlist_wheeldown, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*;*M", bind_id_suggestionlist_drag, true/*has_params*/);
    binder.bind(m_bind_group, "\\r", bind_id_suggestionlist_enter);

    binder.bind(m_bind_group, "^g", bind_id_suggestionlist_escape);
    if (esc)
        binder.bind(m_bind_group, esc, bind_id_suggestionlist_escape);

    binder.bind(m_bind_group, "", bind_id_suggestionlist_catchall);
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_begin_line(const context& context)
{
    assert(!s_suggestionlist);
    s_suggestionlist = this;
    m_buffer = &context.buffer;
    m_printer = &context.printer;
    m_clear_display = false;
    m_scroll_helper.clear();

    m_screen_cols = context.printer.get_columns();
    m_screen_rows = context.printer.get_rows();
    update_layout();
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_end_line()
{
    s_suggestionlist = nullptr;
    m_buffer = nullptr;
    m_printer = nullptr;
    m_clear_display = false;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_need_input(int32& bind_group)
{
    if (!is_select_complete_active())
    {
        enable_suggestion_list(1);

        if (is_active())
            bind_group = m_bind_group;
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_input(const input& _input, result& result, const context& context)
{
    assert(is_active());

    input input = _input;

    // Cancel if no room.
    if (m_visible_rows <= 0)
    {
        cancel(result);
        return;
    }

    bool wrap = !!_rl_menu_complete_wraparound;
    switch (input.id)
    {
    case bind_id_suggestionlist_up:
        if (wrap)
        {
            m_index--;
            if (m_index < -1)
                m_index = m_count - 1;
        }
        else if (m_index >= 0)
        {
            m_index--;
        }
navigated:
        apply_suggestion(m_index);
        update_display();
        break;
    case bind_id_suggestionlist_down:
        m_index++;
        if (m_index >= m_count)
            m_index = wrap ? -1 : m_count - 1;
        goto navigated;

    case bind_id_suggestionlist_pgup:
    case bind_id_suggestionlist_pgdn:
        if (m_index < 0)
        {
            goto catchall;
        }
        else
        {
#if 0
            const int32 y = get_match_row(m_index);
            const int32 rows = min<int32>(m_match_rows, m_visible_rows);
            const int32 scroll_ofs = get_scroll_offset();
            const int32 scroll_rows = (rows - scroll_ofs - 1);
            if (input.id == bind_id_suggestionlist_pgup)
            {
                if (!y)
                {
                    m_index = 0;
                }
                else
                {
                    int32 new_y = max<int32>(0, (y <= m_top + scroll_ofs) ? y - scroll_rows : m_top + scroll_ofs);
                    int32 stride = _rl_print_completions_horizontally ? m_match_cols : 1;
                    m_index += (new_y - y) * stride;
                }
                goto navigated;
            }
            else if (input.id == bind_id_suggestionlist_pgdn)
            {
                if (y == m_match_rows - 1)
                {
                    m_index = count - 1;
                }
                else
                {
                    int32 stride = _rl_print_completions_horizontally ? m_match_cols : 1;
                    int32 new_y = min<int32>(m_match_rows - 1, (y >= m_top + scroll_rows) ? y + scroll_rows : m_top + scroll_rows);
                    int32 new_index = m_index + (new_y - y) * stride;
                    int32 new_top = m_top;
                    if (new_index >= count)
                    {
                        if (_rl_print_completions_horizontally)
                        {
                            new_top = m_match_rows - rows;
                            if (y + 1 < new_y)
                            {
                                new_y--;
                                new_index -= stride;
                            }
                            else
                            {
                                new_index = count - 1;
                            }
                        }
                        else
                        {
                            new_index = count - 1;
                            if (get_match_row(new_index) >= m_top + rows)
                                new_top = min<int32>(get_match_row(new_index),
                                                     m_match_rows - rows);
                        }
                    }
                    m_index = new_index;
                    set_top(max<int32>(0, new_top));
                }
                goto navigated;
            }
#endif
        }
        break;

#if 0
    case bind_id_suggestionlist_leftclick:
    case bind_id_suggestionlist_doubleclick:
    case bind_id_suggestionlist_drag:
        {
            const uint32 now = m_scroll_helper.on_input();

            uint32 p0, p1;
            input.params.get(0, p0);
            input.params.get(1, p1);
            p1 -= m_mouse_offset;
            const uint32 rows = m_displayed_rows;

#ifdef SHOW_VERT_SCROLLBARS
            if (m_vert_scroll_car &&
                (input.id == bind_id_suggestionlist_leftclick ||
                 input.id == bind_id_suggestionlist_doubleclick))
            {
                m_scroll_bar_clicked = (p0 == m_vert_scroll_column && p1 >= 0 && p1 < rows);
            }

            if (m_scroll_bar_clicked)
            {
                const int32 row = min<int32>(max<int32>(p1, 0), rows - 1);
                const int32 index = hittest_scroll_car(row, rows, m_match_rows);
                const int32 stride = _rl_print_completions_horizontally ? m_match_cols : 1;
                m_index = index * stride;
                set_top(min<int32>(max<int32>(m_index - (rows / 2), 0), m_match_rows - rows));
                insert_match();
                update_display();
                break;
            }
#endif

            bool scrolling = false;
            int32 row = p1 + m_top;
            const int32 revert_top = m_top;
            if (p1 < rows)
            {
do_mouse_position:
                const int32 major_stride = _rl_print_completions_horizontally ? m_match_cols : 1;
                const int32 minor_stride = _rl_print_completions_horizontally ? 1 : m_match_rows;
                int32 index = major_stride * row;
                uint32 x1 = 0;
                for (int32 i = 0; i < m_widths.num_columns(); ++i)
                {
                    width_t col_width = m_widths.column_width(i);
                    if (i + 1 >= m_widths.num_columns())
                        col_width += m_screen_cols;
                    else if (scrolling)
                        col_width += m_widths.m_col_padding;
                    if (p0 >= x1 && p0 < x1 + col_width)
                    {
                        m_index = index;
                        m_ignore_scroll_offset = true;
                        if (scrolling)
                            m_scroll_helper.on_scroll(now);
                        if (m_index >= m_matches.get_match_count())
                        {
                            set_top(max<int32>(revert_top, get_match_row(m_matches.get_match_count()) - (rows - 1)));
                            m_index = m_matches.get_match_count() - 1;
                        }
                        insert_match();
                        update_display();
                        if (input.id == bind_id_suggestionlist_doubleclick)
                            goto enter;
                        scrolling = false; // Don't revert top.
                        break;
                    }
                    x1 += m_widths.column_width(i) + m_widths.m_col_padding;
                    index += minor_stride;
                }
            }
            else if (int32(p1) < 0)
            {
                if (input.id == bind_id_suggestionlist_drag)
                {
                    if (m_scroll_helper.can_scroll() && m_top > 0)
                    {
                        set_top(max<int32>(0, m_top - m_scroll_helper.scroll_speed()));
                        row = m_top;
                        scrolling = true;
                        goto do_mouse_position;
                    }
                }
                else
                {
                    cancel(result, true/*can_reactivate*/);
                    result.pass();
                    return;
                }
            }
            else
            {
                if (!m_expanded)
                {
                    m_expanded = true;
                    m_comment_row_displayed = false;
                    m_prev_displayed = -1;
                    update_display();
                }
                else if (input.id == bind_id_suggestionlist_drag)
                {
                    if (m_scroll_helper.can_scroll() && m_top + rows < m_match_rows)
                    {
                        row = m_top + rows;
                        set_top(min<int32>(m_match_rows - rows, m_top + m_scroll_helper.scroll_speed()));
                        scrolling = true;
                        goto do_mouse_position;
                    }
                }
            }
        }
        break;
#endif

#if 0
    case bind_id_suggestionlist_wheelup:
    case bind_id_suggestionlist_wheeldown:
        {
            uint32 p0;
            input.params.get(0, p0);
            const int32 major_stride = _rl_print_completions_horizontally ? m_match_cols : 1;
            const int32 match_row = get_match_row(m_index);
            const int32 prev_index = m_index;
            const int32 prev_top = m_top;
            if (input.id == bind_id_suggestionlist_wheelup)
                m_index -= min<uint32>(match_row, p0) * major_stride;
            else
                m_index += min<uint32>(m_match_rows - 1 - match_row, p0) * major_stride;

            const int32 count = m_matches.get_match_count();
            if (m_index >= count)
            {
                m_index = count - 1;
                const int32 rows = min<int32>(m_match_rows, m_visible_rows);
                if (m_top + rows - 1 == get_match_row(m_index))
                {
                    const int32 max_top = max<int32>(0, m_match_rows - rows);
                    set_top(min<int32>(max_top, m_top + 1));
                }
            }
            assert(!m_ignore_scroll_offset);

            if (m_index != prev_index || m_top != prev_top)
                update_display();
        }
        break;
#endif

    case bind_id_suggestionlist_enter:
        apply_suggestion(m_index);
        cancel(result);
        m_applied = false;
        break;

    case bind_id_suggestionlist_escape:
escape:
        m_applied = false;
        cancel(result);
        return;

    case bind_id_suggestionlist_catchall:
catchall:
        result.set_bind_group(m_prev_bind_group);
        result.pass();
        break;
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_terminal_resize(int32 columns, int32 rows, const context& context)
{
    m_screen_cols = columns;
    m_screen_rows = rows;

    if (is_active())
    {
        m_prev_displayed = -1;
        update_layout();
        update_display();
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_signal(int32 sig)
{
    if (is_active())
    {
        struct dummy_result : public editor_module::result
        {
            virtual void    pass() override {}
            virtual void    loop() override {}
            virtual void    done(bool eof) override {}
            virtual void    redraw() override {}
            virtual int32   set_bind_group(int32 id) override { return 0; }
        };

        dummy_result result;
        cancel(result);
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::cancel(editor_module::result& result, bool can_reactivate)
{
    assert(is_active());

    m_buffer->set_need_draw();

    result.set_bind_group(m_prev_bind_group);
    m_prev_bind_group = -1;

    if (!can_reactivate)
        override_rl_last_func(nullptr, true/*force_when_null*/);

    update_display();

    g_display_manager_no_comment_row = false;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::init_suggestions()
{
    // TODO: get suggestions from suggestion_manager.
    suggestion s;
    m_suggestions.clear();
    s.m_suggestion = "abc def";
    s.m_suggestion_offset = 0;
    s.m_source = "History";
    m_suggestions.emplace_back(std::move(s));
    s.m_suggestion = "abc xyz";
    s.m_suggestion_offset = 0;
    s.m_source = "History";
    m_suggestions.emplace_back(std::move(s));

    m_count = m_suggestions.size();

    m_clear_display = m_any_displayed;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_layout()
{
    // TODO: this kind of has to hide the comment row, but then that disables
    // features like input hints...

    const int32 input_height = (_rl_vis_botlin + 1);
    const int32 header_row = 1;
    int32 available_rows = m_screen_rows - input_height - header_row;
    available_rows = min<>(available_rows, m_screen_rows / 2);
    m_visible_rows = min<>(c_max_suggestion_rows, m_count);

    // At least 3 rows must fit.
    if (m_visible_rows > available_rows && available_rows < 3)
        m_visible_rows = 0;
    m_visible_rows = min<>(m_visible_rows, available_rows);

#ifdef SHOW_VERT_SCROLLBARS
    m_vert_scroll_car = 0;
    m_vert_scroll_column = 0;
#endif
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_top()
{
    const int32 y = m_index;
    if (m_top > y)
    {
        set_top(max<>(0, y));
    }
    else
    {
        const int32 rows = min<int32>(m_count, m_visible_rows);
        int32 top = max<int32>(0, y - (rows - 1));
        if (m_top < top)
            set_top(top);
    }

#if 0
    if (m_expanded)
    {
        const int32 scroll_ofs = get_scroll_offset();
        if (scroll_ofs > 0)
        {
            const int32 y = get_match_row(m_index);
            const int32 last_row = max<int32>(0, m_match_rows - m_visible_rows);
            if (m_top > max(0, y - scroll_ofs))
                set_top(max(0, y - scroll_ofs));
            else if (m_top < min(last_row, y + scroll_ofs - m_displayed_rows + 1))
                set_top(min(last_row, y + scroll_ofs - m_displayed_rows + 1));
        }
    }
#endif

    assert(m_top >= 0);
    assert(m_top <= max<int32>(0, m_count - m_visible_rows));
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_display()
{
#ifdef SHOW_VERT_SCROLLBARS
    m_vert_scroll_car = 0;
    m_vert_scroll_column = 0;
#endif

    if (m_visible_rows <= 0)
        return;

    // Remember the cursor position so it can be restored later to stay
    // consistent with Readline's view of the world.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(h, &csbi);
    COORD restore = csbi.dwCursorPosition;
    const int32 vpos = _rl_last_v_pos;
    const int32 cpos = _rl_last_c_pos;

    // Move cursor after the input line.
    _rl_move_vert(_rl_vis_botlin);

    const char* const normal_color[] = { "\x1b[m", "\x1b[0;100m" };
    const char* const highlight_color[] = { "\x1b[0;1m", "\x1b[0;100;1m" };
    const char* const markup_color[] = { "\x1b[0;33m", "\x1b[0;100;33m" };

    // Display suggestions.
    int32 up = 0;
    if (is_active() && m_count > 0)
    {
        str_moveable tmp;
        const int32 rows = min<>(m_visible_rows, m_count);
        m_displayed_rows = rows;

#ifdef SHOW_VERT_SCROLLBARS
        const int32 reserve_cols = (m_vert_scroll_car ? 3 : 1);
#else
        const int32 reserve_cols = 1;
#endif
        const int32 max_width = min<>(m_screen_cols - reserve_cols, c_max_listview_width);

        rl_crlf();
        up++;

        if (m_clear_display)
        {
            m_printer->print("\x1b[m\x1b[J");
            m_prev_displayed = -1;
            m_clear_display = false;
        }

        // Build the header row.
        str<64> left;
        str<64> right;
        str<16> num;
        if (m_index < 0)
            num = "-";
        else
            num.format("%u", m_index + 1);
        left.format("%s<%s/%u>%s", markup_color[0], num.c_str(), m_count, normal_color[0]);
        // TODO: show sources in right.c_str().
        right.format("<%s...%s>", markup_color[0], normal_color[0]);

        // Show the header row.
        {
            const int32 left_cells = cell_count(left.c_str());
            const int32 right_cells = cell_count(right.c_str());
            const int32 spaces = max_width - (left_cells + right_cells);
            // TODO: what if spaces is negative?
            concat_spaces(tmp, spaces);
        }
        m_printer->print(left.c_str(), left.length());
        m_printer->print(tmp.c_str(), tmp.length());
        m_printer->print(right.c_str(), right.length());

        // Can't update top until after m_displayed_rows is known, so that the
        // scroll offset can be accounted for accurately in all cases.
        update_top();

#ifdef SHOW_VERT_SCROLLBARS
        m_vert_scroll_car = (use_vert_scrollbars() && m_screen_cols >= 8) ? calc_scroll_car_size(rows, m_count) : 0;
        if (m_vert_scroll_car)
            m_vert_scroll_column = m_screen_cols - 2;
        const int32 car_top = calc_scroll_car_offset(m_top, rows, m_count, m_vert_scroll_car);
#endif

        int32 shown = 0;
        for (int32 row = 0; row < rows; row++)
        {
            int32 i = (m_top + row);
            if (i >= m_count)
                break;

            rl_crlf();
            up++;

            // Print entry.
            if (m_prev_displayed < 0 ||
                row + m_top == m_index ||
                row + m_top == m_prev_displayed)
            {
                const bool selected = (i == m_index);

                // TODO: build string with potential ellipses on both ends,
                // and with matching text highlighted...
                tmp.clear();
                tmp = m_suggestions[i].m_suggestion.c_str();

                left.format("%s>%s ", markup_color[selected], normal_color[selected]);
                right.format("[%s%s%s]", markup_color[selected], m_suggestions[i].m_source, normal_color[selected]);
                {
                    const int32 spaces = max_width - (cell_count(left.c_str()) + cell_count(tmp.c_str()) + cell_count(right.c_str()));
                    // TODO: what if spaces is negative?
                    tmp.concat(normal_color[selected]);
                    concat_spaces(tmp, spaces);
                }
                m_printer->print(left.c_str(), left.length());
                m_printer->print(tmp.c_str(), tmp.length());
                m_printer->print(right.c_str(), right.length());

#ifdef SHOW_VERT_SCROLLBARS
                if (m_vert_scroll_car)
                {
#ifdef USE_FULL_SCROLLBAR
                    constexpr bool floating = false;
#else
                    constexpr bool floating = true;
#endif
                    const char* car = get_scroll_car_char(row, car_top, m_vert_scroll_car, floating);
                    if (car)
                    {
                        // Space was reserved by update_layout().
                        tmp.format("%s \x1b[0;90m%s", normal_color[0], car);
                        m_printer->print(tmp.c_str(), tmp.length());
                    }
#ifdef USE_FULL_SCROLLBAR
                    else
                    {
                        // Space was reserved by update_layout().
                        tmp.format("%s \x1b[0;90m\xe2\x94\x82", normal_color[0]);// │
                        m_printer->print(tmp.c_str(), tmp.length());
                    }
#endif
                }
#endif // SHOW_VERT_SCROLLBARS

                // Clear to end of line.
                m_printer->print("\x1b[m\x1b[K");
            }
        }

        assert(!m_clear_display);
        m_prev_displayed = m_index;
        m_any_displayed = true;
    }
    else
    {
        if (m_any_displayed)
        {
            // Move cursor to next line, then clear to end of screen.
            rl_crlf();
            up++;
            m_printer->print("\x1b[m\x1b[J");
        }
        m_prev_displayed = -1;
        m_any_displayed = false;
        m_clear_display = false;
    }

    // Restore cursor position.
    if (up > 0)
    {
        str<16> s;
        s.format("\x1b[%dA", up);
        m_printer->print(s.c_str(), s.length());
    }
    GetConsoleScreenBufferInfo(h, &csbi);
    m_mouse_offset = csbi.dwCursorPosition.Y + 1/*to top item*/;
    _rl_move_vert(vpos);
    _rl_last_c_pos = cpos;
    GetConsoleScreenBufferInfo(h, &csbi);
    restore.Y = csbi.dwCursorPosition.Y;
    SetConsoleCursorPosition(h, restore);
}

//------------------------------------------------------------------------------
void suggestionlist_impl::apply_suggestion(int32 index)
{
#if 0
    assert(is_active());

    if (m_inserted)
    {
        m_buffer->undo();
        m_inserted = false;
        m_quoted = false;
    }

    m_len = 0;

    assert(m_index < m_matches.get_match_count());
    const char* match = m_matches.get_match(m_index);
    match_type type = m_matches.get_match_type(m_index);
    char append_char = m_matches.get_match_append_char(m_index);
    uint8 flags = m_matches.get_match_flags(m_index);

    char qs[2] = {};
    if (match &&
        !rl_completion_found_quote &&
        rl_completer_quote_characters &&
        rl_completer_quote_characters[0] &&
        rl_need_match_quoting(match))
    {
        qs[0] = rl_completer_quote_characters[0];
        m_quoted = true;
    }

    m_buffer->begin_undo_group();
    m_buffer->remove(m_anchor, m_buffer->get_cursor());
    m_buffer->set_cursor(m_anchor);
    m_buffer->insert(qs);
    m_buffer->insert(match);

    bool removed_dir_mark = false;
    if (is_match_type(type, match_type::dir) && !_rl_complete_mark_directories)
    {
        int32 cursor = m_buffer->get_cursor();
        if (cursor >= 2 &&
            m_buffer->get_buffer()[cursor - 1] == '\\' &&
            m_buffer->get_buffer()[cursor - 2] != ':')
        {
            m_buffer->remove(cursor - 1, cursor);
            cursor--;
            m_buffer->set_cursor(cursor);
            removed_dir_mark = true;
        }
    }

    uint32 needle_len = 0;
    if (final)
    {
        int32 nontrivial_lcd = __compare_match(const_cast<char*>(m_needle.c_str()), match);

        bool append_space = false;
        // UGLY: __append_to_match() circumvents the m_buffer abstraction.
        set_matches_lookaside_oneoff(match, type, append_char, flags);
        __append_to_match(const_cast<char*>(match), m_anchor + !!*qs, m_delimiter, *qs, nontrivial_lcd);
        clear_matches_lookaside_oneoff();
        m_point = m_buffer->get_cursor();

        // Pressing Space to insert a final match needs to maybe add a quote,
        // and then maybe add a space, depending on what __append_to_match did.
        if (final == 2 || !is_match_type(type, match_type::dir))
        {
            // A space may or may not be present.  Delete it if one is.
            bool have_space = (m_buffer->get_buffer()[m_point - 1] == ' ');
            bool append_space = (final == 2);
            int32 cursor = m_buffer->get_cursor();
            if (have_space)
            {
                append_space = true;
                have_space = false;
                m_buffer->remove(m_point - 1, m_point);
                m_point--;
                cursor--;
            }

            // Add closing quote if a typed or inserted opening quote is present
            // but no closing quote is present.
            if (!m_quoted &&
                m_anchor > 0 &&
                rl_completion_found_quote &&
                rl_completion_quote_character)
            {
                // Remove a preceding backslash unless it is preceded by colon.
                // Because programs compiled with MSVC treat `\"` as an escape.
                // So `program "c:\dir\" file` is interpreted as having one
                // argument which is `c:\dir" file`.  Be nice and avoid
                // inserting such things on behalf of users.
                //
                // "What's up with the strange treatment of quotation marks and
                // backslashes by CommandLineToArgvW"
                // https://devblogs.microsoft.com/oldnewthing/20100917-00/?p=12833
                //
                // "Everyone quotes command line arguments the wrong way"
                // https://docs.microsoft.com/en-us/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
                if (!removed_dir_mark &&
                    cursor >= 2 &&
                    m_buffer->get_buffer()[cursor - 1] == '\\' &&
                    m_buffer->get_buffer()[cursor - 2] != ':')
                {
                    m_buffer->remove(cursor - 1, cursor);
                    cursor--;
                    removed_dir_mark = true;
                }

                qs[0] = rl_completion_quote_character;
                if (m_buffer->get_buffer()[cursor] != qs[0])
                    m_buffer->insert(qs);
                else if (append_space)
                    m_buffer->set_cursor(++cursor);
            }

            // Add space.
            if (append_space && !have_space)
                m_buffer->insert(" ");
            m_point = m_buffer->get_cursor();
        }
    }
    else
    {
        m_buffer->insert(qs);
        m_point = m_anchor + strlen(qs);
        str_iter lhs(m_needle);
        str_iter rhs(m_buffer->get_buffer() + m_point, m_buffer->get_length() - m_point);
        const int32 cmp_len = str_compare(lhs, rhs);
        if (cmp_len < 0)
            needle_len = m_needle.length();
        else if (cmp_len == m_needle.length())
            needle_len = cmp_len;
    }

    m_point += needle_len;

    m_buffer->set_cursor(m_point);
    m_buffer->end_undo_group();

    update_len(needle_len);
    m_inserted = true;

    const int32 botlin = _rl_vis_botlin;
    m_buffer->draw();
    if (botlin != _rl_vis_botlin)
    {
        // Coax the cursor to the end of the input line.
        const int32 cursor = m_buffer->get_cursor();
        m_buffer->set_cursor(m_buffer->get_length());
        m_buffer->set_need_draw();
        m_buffer->draw();
        // Clear to end of screen.
        m_printer->print("\x1b[J");
        // Restore cursor position.
        m_buffer->set_cursor(cursor);
        m_buffer->set_need_draw();
        m_buffer->draw();
        // Update layout.
        m_prev_displayed = -1;
        m_comment_row_displayed = false;
        update_layout();
    }
#endif
}

//------------------------------------------------------------------------------
void suggestionlist_impl::set_top(int32 top)
{
    assert(top >= 0);
    assert(top <= max<int32>(0, m_count - m_visible_rows));
    if (top != m_top)
    {
        m_top = top;
        m_prev_displayed = -1;
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::reset_top()
{
    m_top = 0;
    m_index = -1;
    m_prev_displayed = -1;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::is_active() const
{
    return m_prev_bind_group >= 0 && m_buffer && m_printer && !m_hide;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::accepts_mouse_input(mouse_input_type type) const
{
    switch (type)
    {
    case mouse_input_type::left_click:
    case mouse_input_type::double_click:
    case mouse_input_type::wheel:
    case mouse_input_type::hwheel:
    case mouse_input_type::drag:
        return true;
    default:
        return false;
    }
}



//------------------------------------------------------------------------------
bool toggle_suggestion_list(editor_module::result& result)
{
    if (!s_suggestionlist)
        return false;

    return s_suggestionlist->toggle(result);
}

//------------------------------------------------------------------------------
extern "C" void enable_suggestion_list(int enable)
{
    if (s_enable_suggestion_list == !!enable)
        return;

    s_enable_suggestion_list = !!enable;

    if (s_suggestionlist && !!enable != s_suggestionlist->is_active())
        s_suggestionlist->enable(enable);
}

#if 0
//------------------------------------------------------------------------------
bool point_in_select_complete(int32 in)
{
    if (!s_selectcomplete)
        return false;
    return s_selectcomplete->point_within(in);
}
#endif

