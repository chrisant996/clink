// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "suggestionlist_impl.h"
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
    bind_id_suggestionlist_f1,
    bind_id_suggestionlist_f2,
    bind_id_suggestionlist_f4,

    bind_id_suggestionlist_catchall,
};



//------------------------------------------------------------------------------
static suggestionlist_impl* s_suggestionlist = nullptr;

//------------------------------------------------------------------------------
suggestionlist_impl::suggestionlist_impl(input_dispatcher& dispatcher)
    : m_dispatcher(dispatcher)
{
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::activate(editor_module::result& result, bool reactivate)
{
    assert(m_buffer);
    if (!m_buffer)
        return false;

    if (reactivate && m_point >= 0 && m_len >= 0 && m_point + m_len <= m_buffer->get_length() && m_inserted)
    {
#ifdef DEBUG
        rollback<int32> rb(m_prev_bind_group, 999999); // Dummy to make assertion happy in insert_needle().
#endif
        insert_needle();
    }

    pause_suggestions(true);

    m_inserted = false;

    init_matches();
    assert(m_anchor >= 0);
    if (m_anchor < 0)
    {
bail_out:
        pause_suggestions(false);
        return false;
    }

    if (!m_matches.get_match_count())
    {
cant_activate:
        m_anchor = -1;
        reset_generate_matches();
        goto bail_out;
    }

    if (reactivate)
    {
        m_comment_row_displayed = false;
    }
    else
    {
        assert(!m_any_displayed);
        assert(!m_comment_row_displayed);
        assert(!m_clear_display);
        m_any_displayed = false;
        m_comment_row_displayed = false;
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

    // Insert first match.

    bool only_one = (m_matches.get_match_count() == 1);
    m_point = m_buffer->get_cursor();
    reset_top();
    insert_match(only_one/*final*/);

    // If there's only one match, then we're done.
    if (only_one)
        cancel(result);
    else
        update_display();

    return true;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::point_within(int32 in) const
{
    return is_active() && m_point >= 0 && in >= m_point && in < m_point + m_len;
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
    binder.bind(m_bind_group, "\\eOP", bind_id_suggestionlist_f1);
    binder.bind(m_bind_group, "\\eOQ", bind_id_suggestionlist_f2);
    binder.bind(m_bind_group, "\\eOS", bind_id_suggestionlist_f4);

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
    m_ignore_scroll_offset = false;
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

    m_ignore_scroll_offset = false;

    bool wrap = !!_rl_menu_complete_wraparound;
    switch (input.id)
    {
    case bind_id_suggestionlist_up:
        m_index--;
        if (m_index < -1)
            m_index = wrap ? count - 1 : 0;
navigated:
        apply_suggestion(m_index);
        update_display();
        break;
    case bind_id_suggestionlist_down:
        m_index++;
        if (m_index >= count)
            m_index = wrap ? -1 : count - 1;
        goto navigated;

#if 0
    case bind_id_suggestionlist_pgup:
    case bind_id_suggestionlist_pgdn:
        {
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
        }
        break;
#endif

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
enter:
        apply_suggestion(m_index, true/*final*/);
        cancel(result);
        m_applied = false;
        break;

#if 0
    case bind_id_suggestionlist_f1:
        if (m_matches.has_descriptions())
        {
            const int32 delta = get_match_row(m_index) - m_top;

            m_desc_below = !m_desc_below;
            m_calc_widths = true;
            update_layout();

            int32 top = max<int32>(0, get_match_row(m_index) - delta);
            const int32 max_top = max<int32>(0, m_match_rows - m_visible_rows);
            if (top > max_top)
                top = max_top;
            set_top(top);

            m_clear_display = true;
            update_display();
        }
        break;
#endif

#if 0
    case bind_id_suggestionlist_f2:
    case bind_id_suggestionlist_f4:
        break;
#endif

    case bind_id_suggestionlist_escape:
revert:
        if (m_applied)
        {
            m_buffer->undo();
            m_applied = false;
        }
        cancel(result);
        return;

    case bind_id_suggestionlist_catchall:
        {
            // TODO:  I don't think the catchall technique works for the
            // suggestionlist, because other inputs need to do their normal
            // things without cancelling the mode.  But some states (not
            // necessarily commands) require cancelling.
        }
        break;
    }
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

    // Leave m_point and m_len alone so that activate() can reactivate if
    // necessary.

    m_buffer->set_need_draw();

    result.set_bind_group(m_prev_bind_group);
    m_prev_bind_group = -1;

    if (!can_reactivate)
        override_rl_last_func(nullptr, true/*force_when_null*/);

    pause_suggestions(false);

    reset_generate_matches();

    update_display();

    g_display_manager_no_comment_row = false;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::init_suggestions()
{
    // TODO: get suggestions from suggestion_manager.

    m_clear_display = m_any_displayed;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_layout()
{
    // TODO: limit to showing 10 rows, plus "tooltip", plus counts "<-/10>etc".
    //m_visible_rows = ?;

    // TODO: this kind of has to hide the comment row, but then that disables
    // features like input hints...

    // At least 5 rows must fit.
    if (m_visible_rows < 5)
        m_visible_rows = 0;

#ifdef SHOW_VERT_SCROLLBARS
    m_vert_scroll_car = 0;
    m_vert_scroll_column = 0;
#endif
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_top()
{
#if 0
    const int32 y = get_match_row(m_index);
    if (m_top > y)
    {
        set_top(y);
    }
    else
    {
        const int32 rows = min<int32>(m_match_rows, m_visible_rows);
        int32 top = max<int32>(0, y - (rows - 1));
        if (m_top < top)
            set_top(top);
    }

    if (m_expanded && !m_ignore_scroll_offset)
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

    assert(m_top >= 0);
    assert(m_top <= max<int32>(0, m_match_rows - m_visible_rows));
#endif
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_display()
{
#if 0
#ifdef SHOW_VERT_SCROLLBARS
    m_vert_scroll_car = 0;
    m_vert_scroll_column = 0;
#endif

    if (m_visible_rows > 0)
    {
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

#ifdef SHOW_DISPLAY_GENERATION
        static char s_chGen = '0';
#endif

        const char* normal_color = "\x1b[m";
        int32 normal_color_len = 3;

        const char* description_color = normal_color;
        int32 description_color_len = normal_color_len;
        if (_rl_description_color)
        {
            description_color = _rl_description_color;
            description_color_len = strlen(description_color);
        }

        str<16> desc_select_color;
        ecma48_processor(description_color, &desc_select_color, nullptr, ecma48_processor_flags::colorless);

        // Display matches.
        int32 up = 0;
        const int32 count = m_matches.get_match_count();
        if (is_active() && count > 0)
        {
            const int32 preview_rows = g_preview_rows.get();
            if (!m_expanded)
            {
                if (preview_rows <= 0 || preview_rows + 1 >= m_visible_rows)
                {
                    m_expanded = true;
                    m_prev_displayed = -1;
                }
                else if (m_index >= 0)
                {
                    if (_rl_print_completions_horizontally)
                        m_expanded = (m_index / m_match_cols) >= preview_rows;
                    else
                        m_expanded = (m_index % m_match_rows) >= preview_rows;
                    if (m_expanded)
                        m_prev_displayed = -1;
                }
                if (m_expanded)
                    m_comment_row_displayed = false;
            }

            const bool show_descriptions = !m_desc_below && m_matches.has_descriptions();
            const bool show_more_comment_row = !m_expanded && (preview_rows + 1 < m_match_rows);
            const int32 rows = min<int32>(m_visible_rows, show_more_comment_row ? preview_rows : m_match_rows);
            m_displayed_rows = rows;

            // Can't update top until after m_displayed_rows is known, so that
            // the scroll offset can be accounted for accurately in all cases.
            update_top();

#ifdef SHOW_VERT_SCROLLBARS
            m_vert_scroll_car = (use_vert_scrollbars() && m_screen_cols >= 8) ? calc_scroll_car_size(rows, m_match_rows) : 0;
            if (m_vert_scroll_car)
                m_vert_scroll_column = m_screen_cols - 2;
            const int32 car_top = calc_scroll_car_offset(m_top, rows, m_match_rows, m_vert_scroll_car);
#endif

            const int32 major_stride = _rl_print_completions_horizontally ? m_match_cols : 1;
            const int32 minor_stride = _rl_print_completions_horizontally ? 1 : m_match_rows;
#ifdef DEBUG
            const int32 col_extra = m_col_extra;
#else
            const int32 col_extra = 0;
#endif

            int32 shown = 0;
            for (int32 row = 0; row < rows; row++)
            {
                int32 i = (m_top + row) * major_stride;
                if (i >= count)
                    break;

                rl_crlf();
                up++;

                if (m_clear_display && row == 0)
                {
                    m_printer->print("\x1b[m\x1b[J");
                    m_comment_row_displayed = false;
                    m_prev_displayed = -1;
                    m_clear_display = false;
                }

                // Count matches on the row.
                if (show_more_comment_row)
                {
                    assert(m_top == 0);
                    int32 t = i;
                    for (int32 col = 0; col < m_match_cols; col++)
                    {
                        if (t >= count)
                            break;
                        shown++;
                        t += minor_stride;
                    }
                }

                // Print matches on the row.
                if (m_prev_displayed < 0 ||
                    row + m_top == get_match_row(m_index) ||
                    row + m_top == get_match_row(m_prev_displayed))
                {
                    str<> truncated;
                    str<> tmp;
                    reset_tmpbuf();
#ifdef SHOW_DISPLAY_GENERATION
                    append_tmpbuf_char(s_chGen);
#endif
                    for (int32 col = 0; col < m_match_cols; col++)
                    {
                        if (i >= count)
                            break;

#ifdef SHOW_VERT_SCROLLBARS
                        const int32 reserve_cols = (m_vert_scroll_car ? 3 : 1);
#else
                        const int32 reserve_cols = 1;
#endif
                        const bool right_justify = m_widths.m_right_justify;
                        const int32 col_max = ((show_descriptions && !right_justify) ?
                                               m_screen_cols - reserve_cols :
                                               min<int32>(m_screen_cols - 1, m_widths.column_width(col))) - col_extra;

                        const int32 selected = (i == m_index);
                        const char* const display = m_matches.get_match_display(i);
                        const match_type type = m_matches.get_match_type(i);
                        const bool append = m_matches.is_append_display(i);

                        mark_tmpbuf();
                        int32 printed_len;
                        if (use_display(append, type, i))
                        {
                            printed_len = 0;
                            if (append)
                            {
                                const char* match = m_matches.get_match(i);
                                char* temp = __printable_part(const_cast<char*>(match));
                                printed_len = append_filename(temp, match, 0, 0, type, selected, nullptr);
                            }
                            append_display(display, selected, append ? _rl_arginfo_color : _rl_filtered_color);
                            printed_len += m_matches.get_match_visible_display(i);

                            if (printed_len > col_max || selected)
                            {
                                str<> buf(get_tmpbuf_rollback());
                                const char* temp = buf.c_str();

                                if (printed_len > col_max)
                                {
                                    printed_len = ellipsify(temp, col_max, truncated, false/*expand_ctrl*/);
                                    temp = truncated.c_str();
                                }
                                if (selected)
                                {
                                    ecma48_processor(temp, &tmp, nullptr, ecma48_processor_flags::colorless);
                                    temp = tmp.c_str();
                                }

                                rollback_tmpbuf();
                                append_display(temp, selected, "");
                            }
                        }
                        else
                        {
                            int32 vis_stat_char;
                            char* temp = m_matches.is_display_filtered() ? const_cast<char*>(display) : __printable_part(const_cast<char*>(display));
                            printed_len = append_filename(temp, display, 0, 0, type, selected, &vis_stat_char);
                            if (printed_len > col_max)
                            {
                                rollback_tmpbuf();
                                ellipsify(temp, col_max - !!vis_stat_char, truncated, true/*expand_ctrl*/);
                                temp = truncated.data();
                                printed_len = append_filename(temp, display, 0, 0, type, selected, nullptr);
                            }
                        }

                        const int32 next = i + minor_stride;
                        const width_t max_match_len = m_widths.max_match_len(col);

                        if (show_descriptions && !right_justify)
                        {
                            pad_filename(printed_len, -max_match_len, selected);
                            printed_len = max_match_len;
                        }

                        const char* desc = m_desc_below ? nullptr : m_matches.get_match_description(i);
                        if (desc && *desc)
                        {
                            // Leave at least one space at end of line, or else
                            // "\x1b[K" can erase part of the intended output.
#ifdef USE_DESC_PARENS
                            const int32 parens = right_justify ? 2 : 0;
#else
                            const int32 parens = 0;
#endif
                            const int32 pad_to = (right_justify ?
                                max<int32>(printed_len + m_widths.m_desc_padding, col_max - (m_matches.get_match_visible_description(i) + parens)) :
                                max_match_len + m_widths.m_desc_padding);
                            if (pad_to < m_screen_cols - 1)
                            {
                                const bool use_sel_color = (selected && right_justify);
                                const char* const dc = use_sel_color ? desc_select_color.c_str() : description_color;
                                const int32 dc_len = use_sel_color ? desc_select_color.length() : description_color_len;
                                pad_filename(printed_len, pad_to, -1);
                                printed_len = pad_to + parens;
                                append_tmpbuf_string(dc, dc_len);
                                if (parens)
                                {
                                    append_tmpbuf_string("(", 1);
                                    mark_tmpbuf();
                                }
                                printed_len += ellipsify_to_callback(desc, col_max - printed_len, false/*expand_ctrl*/,
                                    use_sel_color ? append_tmpbuf_string_colorless : append_tmpbuf_string);
                                if (parens)
                                {
                                    if (strchr(get_tmpbuf_rollback(), '\x1b'))
                                        append_tmpbuf_string(dc, dc_len);
                                    append_tmpbuf_string(")", 1);
                                }
                                if (!selected || !right_justify)
                                    append_tmpbuf_string(normal_color, normal_color_len);
                            }
                        }

#ifdef DEBUG
                        if (col_extra)
                        {
                            pad_filename(printed_len, col_max + 1, -1);
                            printed_len = col_max + col_extra;

                            if (!selected)
                                append_tmpbuf_string("\x1b[36m", 5);

                            char _extra[3];
                            str_base extra(_extra);
                            extra.format("%2x", type);
                            append_tmpbuf_string(_extra, 2);
                        }
#endif

                        const bool last_col = (col + 1 >= m_match_cols || next >= count);
                        if (!last_col || selected)
                            pad_filename(printed_len, -col_max, selected);
                        if (!last_col)
                            pad_filename(0, m_widths.m_col_padding, 0);

                        i = next;
                    }

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
                            // Space was reserved by update_layout() or col_max.
                            const uint32 pad_to = m_screen_cols - 2;
                            const uint32 len = calc_tmpbuf_cell_count();
                            if (pad_to >= len)
                            {
                                make_spaces(pad_to - len, tmp);
                                append_tmpbuf_string(tmp.c_str(), tmp.length());
                                append_tmpbuf_string("\x1b[0;90m", 7);
                                append_tmpbuf_string(car, -1);          // ┃ or etc
                            }
                        }
#ifdef USE_FULL_SCROLLBAR
                        else
                        {
                            // Space was reserved by update_layout() or col_max.
                            const uint32 pad_to = m_screen_cols - 2;
                            const uint32 len = calc_tmpbuf_cell_count();
                            if (pad_to >= len)
                            {
                                make_spaces(pad_to - len, tmp);
                                append_tmpbuf_string(tmp.c_str(), tmp.length());
                                append_tmpbuf_string("\x1b[0;90m", 7);
                                append_tmpbuf_string("\xe2\x94\x82", 3);// │
                            }
                        }
#endif
                    }
#endif // SHOW_VERT_SCROLLBARS

                    flush_tmpbuf();

                    // Clear to end of line.
                    m_printer->print("\x1b[m\x1b[K");
                }
            }

            if (show_more_comment_row || (m_visible_rows < m_match_rows))
            {
                rl_crlf();
                up++;

                if (!m_comment_row_displayed)
                {
                    str<> tmp;
                    if (!m_expanded)
                    {
                        const int32 more = m_matches.get_match_count() - shown;
                        tmp.format("\x1b[%sm... and %u more matches ...\x1b[m\x1b[K", g_color_comment_row.get(), more);
                    }
                    else
                    {
                        tmp.format("\x1b[%smrows %u to %u of %u\x1b[m\x1b[K", g_color_comment_row.get(), m_top + 1, m_top + m_visible_rows, m_match_rows);
                    }
                    m_printer->print(tmp.c_str(), tmp.length());
                    m_comment_row_displayed = true;
                }
            }

            assert(!m_clear_display);
            m_prev_displayed = m_index;
            m_any_displayed = true;

            // Show match description.
            if (m_desc_below && m_matches.has_descriptions())
            {
                rl_crlf();
                m_printer->print("\x1b[m\x1b[J");
                rl_crlf();
                up += 2;

                static const char c_footer[] = "\x1b[7mF1\x1b[27m-InlineDescs";
                int32 footer_cols = cell_count(c_footer);
                if (footer_cols + 2 > m_screen_cols / 2)
                    footer_cols = 0;

                const int32 fit_cols = m_screen_cols - 1 - (footer_cols ? footer_cols + 2 : 0);

                str<> s;
                if (m_index >= 0 && m_index < m_matches.get_match_count())
                {
                    const char* desc = m_matches.get_match_description(m_index);
                    if (desc && *desc)
                        ellipsify(desc, fit_cols, s, false);
                }

                m_printer->print(description_color, description_color_len);
                m_printer->print(s.c_str(), s.length());
                if (footer_cols)
                {
                    s.format("\x1b[%uG", m_screen_cols - footer_cols);
                    m_printer->print(description_color, description_color_len);
                    m_printer->print(s.c_str(), s.length());
                    m_printer->print(c_footer);
                }
                m_printer->print("\x1b[m");
            }
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
            m_comment_row_displayed = false;
            m_expanded = false;
            m_clear_display = false;
        }

#ifdef SHOW_DISPLAY_GENERATION
        s_chGen++;
        if (s_chGen > 'Z')
            s_chGen = '0';
#endif

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
#endif
}

//------------------------------------------------------------------------------
void suggestionlist_impl::apply_suggestion(int32 index, int32 final)
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
#if 0
    assert(top >= 0);
    assert(top <= max<int32>(0, m_match_rows - m_visible_rows));
    if (top != m_top)
    {
        m_top = top;
        m_prev_displayed = -1;
        m_comment_row_displayed = false;
    }
#endif
}

//------------------------------------------------------------------------------
void suggestionlist_impl::reset_top()
{
    m_top = 0;
    m_index = 0;
    m_prev_displayed = -1;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::is_active() const
{
#if 0
    return m_prev_bind_group >= 0 && m_buffer && m_printer && m_anchor >= 0 && m_point >= m_anchor;
#endif
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



#if 0
//------------------------------------------------------------------------------
bool activate_select_complete(editor_module::result& result, bool reactivate)
{
    if (!s_selectcomplete)
        return false;

    return s_selectcomplete->activate(result, reactivate);
}

//------------------------------------------------------------------------------
bool point_in_select_complete(int32 in)
{
    if (!s_selectcomplete)
        return false;
    return s_selectcomplete->point_within(in);
}
#endif

