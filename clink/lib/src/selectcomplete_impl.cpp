// Copyright (c) 2021 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "selectcomplete_impl.h"
#include "binder.h"
#include "editor_module.h"
#include "line_buffer.h"
#include "line_state.h"
#include "matches.h"
#include "matches_lookaside.h"
#include "match_pipeline.h"
#include "display_matches.h"
#include "display_readline.h"
#include "column_widths.h"
#include "ellipsify.h"
#include "match_adapter.h"
#include "line_editor_integration.h"
#include "rl_integration.h"
#include "suggestions.h"
#include "slash_translation.h"
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
int __compare_match(char* text, const char* match);
int __append_to_match(char* text, int orig_start, int delimiter, int quote_char, int nontrivial_match);
char* __printable_part(char* text);
void __set_completion_defaults(int what_to_do);
int __get_y_or_n(int for_pager);
extern int _rl_last_v_pos;
};



//------------------------------------------------------------------------------
static setting_int g_preview_rows(
    "match.preview_rows",
    "Preview rows",
    "The number of rows to show as a preview when using the 'clink-select-complete'\n"
    "command (bound by default to Ctrl+Shift+Space).  When this is 0, all rows are\n"
    "shown and if there are too many matches it instead prompts first like the\n"
    "'complete' command does.  Otherwise it shows the specified number of rows as\n"
    "a preview without prompting, and it expands to show the full set of matches\n"
    "when the selection is moved past the preview rows.",
    5);

static setting_int g_max_rows(
    "match.max_rows",
    "Max rows in clink-select-complete",
    "The maximum number of rows of items 'clink-select-complete' can show.  When\n"
    "this is 0, the limit is the terminal height.",
    0);

setting_color g_color_comment_row(
    "color.comment_row",
    "Color for comment row",
    "The color for the comment row.  During 'clink-select-complete' the comment\n"
    "row shows the \"and N more matches\" or \"rows X to Y of Z\" messages.  It\n"
    "can also show how history expansion will be applied at the cursor.",
    "bright white on cyan");

setting_bool g_match_best_fit(
    "match.fit_columns",
    "Fits match columns to screen width",
    "When displaying match completions, this calculates column widths to fit as\n"
    "many as possible on the screen.",
    true);

setting_int g_match_limit_fitted(
    "match.max_fitted_matches",
    "Limits fitted columns by number of matches",
    "When 'match.fit_columns' is enabled, this disables calculating column widths\n"
    "when the number of matches exceeds this value.  The default is 0 (unlimited).\n"
    "Depending on the screen width and CPU speed, setting a limit may avoid delays.",
    0);

setting_int g_clink_scroll_offset(
    "clink.scroll_offset",
    "Scroll offset above/below selected item in list",
    "Number of screen lines to show above or below a selected item in popup\n"
    "lists or the clink-select-complete command.  The list scrolls up or down\n"
    "as needed to maintain the scroll offset (except after a mouse click).\n",
    3);

extern setting_bool g_match_expand_abbrev;



//------------------------------------------------------------------------------
enum {
    bind_id_selectcomplete_next = 60,
    bind_id_selectcomplete_prev,
    bind_id_selectcomplete_up,
    bind_id_selectcomplete_down,
    bind_id_selectcomplete_left,
    bind_id_selectcomplete_right,
    bind_id_selectcomplete_pgup,
    bind_id_selectcomplete_pgdn,
    bind_id_selectcomplete_first,
    bind_id_selectcomplete_last,
    bind_id_selectcomplete_leftclick,
    bind_id_selectcomplete_doubleclick,
    bind_id_selectcomplete_wheelup,
    bind_id_selectcomplete_wheeldown,
    bind_id_selectcomplete_wheelleft,
    bind_id_selectcomplete_wheelright,
    bind_id_selectcomplete_drag,
    bind_id_selectcomplete_backspace,
    bind_id_selectcomplete_delete,
    bind_id_selectcomplete_space,
    bind_id_selectcomplete_enter,
    bind_id_selectcomplete_slash,
    bind_id_selectcomplete_backslash,
    bind_id_selectcomplete_quote,
    bind_id_selectcomplete_escape,
    bind_id_selectcomplete_f1,

    bind_id_selectcomplete_catchall,
};



//------------------------------------------------------------------------------
#ifdef FISH_ARROW_KEYS

static void move_selection_lower(int32& index, int32& major, int32& minor, const int32 count)
{
    if (!index)
    {
        if (_rl_menu_complete_wraparound)
            goto find_wrapped_end;
        return;
    }

    index -= major;

    if (index < 0)
    {
find_wrapped_end:
        index--;
        index += major * minor;
        while (index >= count)
            index -= major;
    }
}

static bool move_selection_higher(int32& index, int32& major, int32& minor, const int32 count, bool& latched)
{
    if (latched)
        return false;

    if (index + major >= count && (index + 1) % major == 0)
    {
        if (_rl_menu_complete_wraparound)
        {
            index = 0;
            return true;
        }
        index = count - 1;
        latched = true;
        return true;
    }

    index += major;

    if (index >= count)
        index = (index + 1) % major;

    return true;
}

#endif



//------------------------------------------------------------------------------
static selectcomplete_impl* s_selectcomplete = nullptr;

//------------------------------------------------------------------------------
selectcomplete_impl::selectcomplete_impl(input_dispatcher& dispatcher)
    : m_dispatcher(dispatcher)
{
}

//------------------------------------------------------------------------------
bool selectcomplete_impl::activate(editor_module::result& result, bool reactivate)
{
    assert(m_buffer);
    if (!m_buffer)
        return false;

    enable_suggestion_list(0);

    if (reactivate && m_point >= 0 && m_len >= 0 && m_point + m_len <= m_buffer->get_length() && m_inserted)
    {
#ifdef DEBUG
        rollback<int32> rb(m_prev_bind_group, 999999); // Dummy to make assertion happy in insert_needle().
#endif
        insert_needle();
    }

    pause_suggestions(true);

    m_inserted = false;
    m_quoted = false;

    m_anchor = -1;
    m_delimiter = 0;
    if (!is_regen_blocked())
        reset_generate_matches();

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
        m_expanded = true;
    }
    else
    {
        assert(!m_any_displayed);
        assert(!m_comment_row_displayed);
        assert(!m_expanded);
        assert(!m_clear_display);
        m_init_desc_below = true;
        m_any_displayed = false;
        m_comment_row_displayed = false;
        m_can_prompt = g_preview_rows.get() <= 0;
        m_expanded = false;
        m_clear_display = false;
    }

    // Make sure there's room.
    update_layout();
    if (m_visible_rows <= 0)
        goto cant_activate;

    // Depending on the mode, either show the first few entries and don't expand
    // until the selection reaches an entry not yet visible, or just prompt if
    // there are too many matches.
    if (!m_expanded &&
        m_can_prompt &&
        (rl_completion_auto_query_items ?
            (m_match_rows > m_visible_rows) :
            (rl_completion_query_items > 0 && m_matches.get_match_count() >= rl_completion_query_items)))
    {
        // I gave up trying to coax Readline into righting the cursor position
        // purely using only ANSI codes.
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(h, &csbi);
        COORD restore = csbi.dwCursorPosition;

        // Move cursor after the input line.
        int32 vpos = _rl_last_v_pos;
        _rl_move_vert(_rl_vis_botlin);
        rl_crlf();

        // Show prompt.
        if (_rl_pager_color)
            _rl_print_pager_color();
        str<> prompt;
        prompt.format("Display all %d possibilities? (y or n) _", m_matches.get_match_count());
        m_printer->print(prompt.c_str(), prompt.length());
        if (_rl_pager_color)
            m_printer->print("\x1b[m");

        // Restore cursor position.
        m_printer->print("\x1b[A");
        _rl_move_vert(vpos);
        GetConsoleScreenBufferInfo(h, &csbi);
        restore.Y = csbi.dwCursorPosition.Y;
        SetConsoleCursorPosition(h, restore);

        // Wait for input.
        bool yes = __get_y_or_n(0) > 0;

        // Erase prompt.
        _rl_move_vert(_rl_vis_botlin);
        rl_crlf();
        m_printer->print("\x1b[K");
        SetConsoleCursorPosition(h, restore);

        if (!yes)
            goto cant_activate;

        m_expanded = true;
        m_can_prompt = false;
    }

    // Disable the comment row.
    g_display_manager_no_comment_row = true;

    // Activate key bindings.
    assert(m_prev_bind_group < 0);
    m_prev_bind_group = result.set_bind_group(m_bind_group);
    m_was_backspace = false;

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
bool selectcomplete_impl::point_within(int32 in) const
{
    return is_active() && m_point >= 0 && in >= m_point && in < m_point + m_len;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::bind_input(binder& binder)
{
    const char* esc = get_bindable_esc();

    m_bind_group = binder.create_group("selectcomplete");
    binder.bind(m_bind_group, "\\t", bind_id_selectcomplete_next);
    binder.bind(m_bind_group, "\\e[Z", bind_id_selectcomplete_prev);
    binder.bind(m_bind_group, "\\e[A", bind_id_selectcomplete_up);
    binder.bind(m_bind_group, "\\e[B", bind_id_selectcomplete_down);
    binder.bind(m_bind_group, "\\e[D", bind_id_selectcomplete_left);
    binder.bind(m_bind_group, "\\e[C", bind_id_selectcomplete_right);
    binder.bind(m_bind_group, "\\e[5~", bind_id_selectcomplete_pgup);
    binder.bind(m_bind_group, "\\e[6~", bind_id_selectcomplete_pgdn);
    binder.bind(m_bind_group, "\\e[1;5H", bind_id_selectcomplete_first);
    binder.bind(m_bind_group, "\\e[1;5F", bind_id_selectcomplete_last);
    binder.bind(m_bind_group, "\\e[$*;*L", bind_id_selectcomplete_leftclick, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*;*D", bind_id_selectcomplete_doubleclick, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*A", bind_id_selectcomplete_wheelup, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*B", bind_id_selectcomplete_wheeldown, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*<", bind_id_selectcomplete_wheelleft, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*>", bind_id_selectcomplete_wheelright, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*;*M", bind_id_selectcomplete_drag, true/*has_params*/);
    binder.bind(m_bind_group, "^h", bind_id_selectcomplete_backspace);
    binder.bind(m_bind_group, "\\e[3~", bind_id_selectcomplete_delete);
    binder.bind(m_bind_group, " ", bind_id_selectcomplete_space);
    binder.bind(m_bind_group, "\\r", bind_id_selectcomplete_enter);
    binder.bind(m_bind_group, "/", bind_id_selectcomplete_slash);
    binder.bind(m_bind_group, "\\", bind_id_selectcomplete_backslash);
    binder.bind(m_bind_group, "\"", bind_id_selectcomplete_quote);
    binder.bind(m_bind_group, "\\eOP", bind_id_selectcomplete_f1);

    binder.bind(m_bind_group, "^g", bind_id_selectcomplete_escape);
    if (esc)
        binder.bind(m_bind_group, esc, bind_id_selectcomplete_escape);

    binder.bind(m_bind_group, "", bind_id_selectcomplete_catchall);
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_begin_line(const context& context)
{
    assert(!s_selectcomplete);
    s_selectcomplete = this;
    m_buffer = &context.buffer;
    m_init_matches = &context.matches;
    m_matches.set_matches(m_init_matches);
    m_data.clear();
    m_printer = &context.printer;
    m_anchor = -1;
    m_any_displayed = false;
    m_comment_row_displayed = false;
    m_can_prompt = true;
    m_expanded = false;
    m_clear_display = false;
    m_scroll_helper.clear();

#ifdef FISH_ARROW_KEYS
    m_prev_latched = false;
    m_prev_input_id = 0;
#endif

    m_screen_cols = context.printer.get_columns();
    m_screen_rows = context.printer.get_rows();
    m_desc_below = false;
    m_init_desc_below = true;
    update_layout();
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_end_line()
{
    assert(!m_any_displayed);
    assert(!m_comment_row_displayed);
    assert(!m_expanded);
    s_selectcomplete = nullptr;
    m_buffer = nullptr;
    m_init_matches = nullptr;
    m_matches.set_matches(nullptr);
    m_data.clear();
    m_printer = nullptr;
    m_anchor = -1;
    m_desc_below = false;
    m_init_desc_below = true;
    m_can_prompt = true;
    m_clear_display = false;
    m_ignore_scroll_offset = false;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_need_input(int32& bind_group)
{
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_input(const input& _input, result& result, const context& context)
{
    assert(is_active());

    input input = _input;

#ifdef FISH_ARROW_KEYS
    const uint8 prev_input_id = m_prev_input_id;
    if (m_prev_input_id != input.id)
    {
        if (input.id != bind_id_selectcomplete_down && input.id != bind_id_selectcomplete_right)
            m_prev_latched = false;
        m_prev_input_id = input.id;
    }
#endif

    // Convert double Backspace into Escape.
    if (input.id != bind_id_selectcomplete_backspace)
        m_was_backspace = false;
    else if (m_was_backspace)
    {
revert:
        if (m_inserted)
        {
            m_buffer->undo();
            m_inserted = false;
        }
        cancel(result);
        return;
    }

    // Cancel if no matches (which shouldn't be able to happen here).
    int32 count = m_matches.get_match_count();
    if (!count)
    {
        assert(count);
        cancel(result);
        return;
    }

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
    case bind_id_selectcomplete_next:
next:
        m_index++;
        if (m_index >= count)
            m_index = wrap ? 0 : count - 1;
navigated:
        insert_match();
        update_display();
        break;
    case bind_id_selectcomplete_prev:
prev:
        m_index--;
        if (m_index < 0)
            m_index = wrap ? count - 1 : 0;
        goto navigated;

#ifdef FISH_ARROW_KEYS
#pragma region fish arrow keys

    case bind_id_selectcomplete_up:
        if (_rl_print_completions_horizontally)
        {
            move_selection_lower(m_index, m_match_cols, m_match_rows, count);
            goto navigated;
        }
        else
        {
arrow_prev:
            wrap = !!_rl_menu_complete_wraparound;
            goto prev;
        }
    case bind_id_selectcomplete_down:
        if (_rl_print_completions_horizontally)
        {
            if (move_selection_higher(m_index, m_match_cols, m_match_rows, count, m_prev_latched))
                goto navigated;
            break;
        }
        else
        {
arrow_next:
            if (!_rl_menu_complete_wraparound && m_index == count - 1)
                m_prev_latched = true;
            wrap = !!_rl_menu_complete_wraparound;
            goto next;
        }

    case bind_id_selectcomplete_left:
    case bind_id_selectcomplete_wheelleft:
        if (_rl_print_completions_horizontally)
            goto arrow_prev;
        move_selection_lower(m_index, m_match_rows, m_match_cols, count);
        goto navigated;
    case bind_id_selectcomplete_right:
    case bind_id_selectcomplete_wheelright:
        if (_rl_print_completions_horizontally)
            goto arrow_next;
        if (move_selection_higher(m_index, m_match_rows, m_match_cols, count, m_prev_latched))
            goto navigated;
        break;

#pragma endregion fish arrow keys
#else // !FISH_ARROW_KEYS
#pragma region powershell arrow keys

    case bind_id_selectcomplete_up:
        if (m_index == 0)
            break;
        if (_rl_print_completions_horizontally)
        {
            m_index -= m_match_cols;
            if (m_index < 0)
                m_index = 0;
            goto navigated;
        }
        wrap = false;
        goto prev;
    case bind_id_selectcomplete_down:
        if (m_index == count - 1)
            break;
        if (_rl_print_completions_horizontally)
        {
            m_index += m_match_cols;
            if (m_index >= count)
                m_index = count - 1;
            goto navigated;
        }
        wrap = false;
        goto next;

    case bind_id_selectcomplete_left:
        if (m_index == 0)
            break;
        if (!_rl_print_completions_horizontally)
        {
            m_index -= m_match_rows;
            if (m_index < 0)
                m_index = 0;
            goto navigated;
        }
        wrap = false;
        goto prev;
    case bind_id_selectcomplete_right:
        if (m_index == count - 1)
            break;
        if (!_rl_print_completions_horizontally)
        {
            m_index += m_match_rows;
            if (m_index >= count)
                m_index = count - 1;
            goto navigated;
        }
        wrap = false;
        goto next;

#pragma endregion powershell arrow keys
#endif // !FISH_ARROW_KEYS

    case bind_id_selectcomplete_pgup:
    case bind_id_selectcomplete_pgdn:
        {
            const int32 y = get_match_row(m_index);
            const int32 rows = min<int32>(m_match_rows, m_visible_rows);
            const int32 scroll_ofs = get_scroll_offset();
            const int32 scroll_rows = (rows - scroll_ofs - 1);
            if (input.id == bind_id_selectcomplete_pgup)
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
            else if (input.id == bind_id_selectcomplete_pgdn)
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

    case bind_id_selectcomplete_first:
        m_index = 0;
        goto navigated;
    case bind_id_selectcomplete_last:
        if (count > 0)
        {
            m_index = count - 1;
            const int32 rows = min<int32>(m_match_rows, m_visible_rows);
            int32 row = get_match_row(m_index);
            if (row + 1 < m_match_rows)
                row++;
            set_top(max<int32>(0, row - (rows - 1)));
            goto navigated;
        }
        break;

    case bind_id_selectcomplete_leftclick:
    case bind_id_selectcomplete_doubleclick:
    case bind_id_selectcomplete_drag:
        {
            const uint32 now = m_scroll_helper.on_input();

            uint32 p0, p1;
            input.params.get(0, p0);
            input.params.get(1, p1);
            p1 -= m_mouse_offset;
            const uint32 rows = m_displayed_rows;

#ifdef SHOW_VERT_SCROLLBARS
            if (m_vert_scroll_car &&
                (input.id == bind_id_selectcomplete_leftclick ||
                 input.id == bind_id_selectcomplete_doubleclick))
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
                        if (input.id == bind_id_selectcomplete_doubleclick)
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
                if (input.id == bind_id_selectcomplete_drag)
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
                else if (input.id == bind_id_selectcomplete_drag)
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

    case bind_id_selectcomplete_wheelup:
    case bind_id_selectcomplete_wheeldown:
        {
            uint32 p0;
            input.params.get(0, p0);
            const int32 major_stride = _rl_print_completions_horizontally ? m_match_cols : 1;
            const int32 match_row = get_match_row(m_index);
            const int32 prev_index = m_index;
            const int32 prev_top = m_top;
            if (input.id == bind_id_selectcomplete_wheelup)
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

    case bind_id_selectcomplete_backspace:
        if (m_needle.length() <= m_lcd)
        {
            m_was_backspace = true;
        }
        else if (m_needle.length())
        {
            int32 point = _rl_find_prev_mbchar(const_cast<char*>(m_needle.c_str()), m_needle.length(), MB_FIND_NONZERO);
            m_needle.truncate(point);
            goto update_needle;
        }
        break;

    case bind_id_selectcomplete_delete:
delete_completion:
        insert_needle();
        cancel(result);
        m_inserted = false; // A subsequent activation should not resume.
        break;

    case bind_id_selectcomplete_space:
        insert_match(2/*final*/);
        cancel(result);
        m_inserted = false; // A subsequent activation should not resume.
        break;

    case bind_id_selectcomplete_enter:
enter:
        insert_match(true/*final*/);
        cancel(result);
        m_inserted = false; // A subsequent activation should not resume.
        break;

    case bind_id_selectcomplete_slash:
        if (is_match_type(m_matches.get_match_type(m_index), match_type::dir))
        {
            m_buffer->set_cursor(m_point + m_len + m_quoted); // Past quotes, if any.
            cancel(result);
            m_inserted = false; // A subsequent activation should not resume.
            result.pass();
            break;
        }
append_not_dup:
        if (m_needle.length() && path::is_separator(m_needle.c_str()[m_needle.length() - 1]))
        {
            m_needle.concat(input.keys, input.len);
            goto delete_completion;
        }
        goto append_to_needle;
    case bind_id_selectcomplete_backslash:
        if (is_match_type(m_matches.get_match_type(m_index), match_type::dir))
        {
            m_buffer->set_cursor(m_point + m_len); // Inside quotes, if any.
            if (m_point + m_len > 0 && m_buffer->get_buffer()[m_point + m_len - 1] != '\\')
                m_buffer->insert("\\");
            cancel(result);
            m_inserted = false; // A subsequent activation should not resume.
            break;
        }
        goto append_not_dup;

    case bind_id_selectcomplete_quote:
        insert_needle();
        cancel(result);
        m_inserted = false; // A subsequent activation should not resume.
        result.pass();
        break;

    case bind_id_selectcomplete_f1:
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

    case bind_id_selectcomplete_escape:
        goto revert;

    case bind_id_selectcomplete_catchall:
        {
            // Figure out whether the input is text to be inserted.
            {
                str_iter iter(input.keys, input.len);
                while (iter.more())
                {
                    uint32 c = iter.next();
                    if (c < ' ' || c == 0x7f)
                    {
                        cancel(result, true/*can_reactivate*/);
                        result.pass();
                        return;
                    }
                }
            }

            // Insert the text.
append_to_needle:
            m_needle.concat(input.keys, input.len);
update_needle:
            reset_top();
            insert_needle();
            update_matches();
            update_layout();
            update_display();
            if (m_matches.get_match_count())
                insert_match();
            else
                cancel(result);
        }
        break;
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
    reset_top();
    m_anchor = line.get_end_word_offset();

    // Update the needle regardless whether active.  This is so update_matches()
    // can filter the filtered matches based on the initial needle.  Because the
    // matches were initially expanded with "g" matching ".git" and "getopt\"
    // but only an explicit wildcard (e.g. "*g") should accept ".git".
    m_needle = needle;
    update_len(m_needle.length());

    assert(!m_ignore_scroll_offset);
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_terminal_resize(int32 columns, int32 rows, const context& context)
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
void selectcomplete_impl::on_signal(int32 sig)
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
void selectcomplete_impl::cancel(editor_module::result& result, bool can_reactivate)
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

    m_matches.reset();
    assert(m_matches.get_matches() == m_init_matches);
    m_data.clear();

    g_display_manager_no_comment_row = false;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::init_matches()
{
    m_ignore_scroll_offset = false;

    m_matches.reset();
    assert(m_init_matches);
    assert(m_matches.get_matches() == m_init_matches);
    m_data.clear();

    ::force_update_internal(true);
    m_matches.set_regen_matches(nullptr);

    __set_completion_defaults('%');
    rl_completion_type = '!';

    int32 found_quote = 0;
    int32 quote_char = 0;

    if (m_buffer->get_cursor())
    {
        int32 tmp = m_buffer->get_cursor();
        quote_char = _rl_find_completion_word(&found_quote, &m_delimiter);
        m_buffer->set_cursor(tmp);
    }

    rl_completion_found_quote = found_quote;
    rl_completion_quote_character = quote_char;

    ::update_matches();

    // Expand an abbreviated path.
    str_moveable tmp;
    bool override = false;
    override_match_line_state omls;
    const char* needle = m_needle.c_str();
    if (g_match_expand_abbrev.get() && !m_matches.get_match_count())
    {
        tmp.concat(m_buffer->get_buffer() + m_anchor, m_point - m_anchor);

        if (rl_complete_with_tilde_expansion && tmp.c_str()[0] == '~')
        {
            const bool just_tilde = !tmp.c_str()[1];
            if (path::tilde_expand(tmp) && just_tilde)
                path::maybe_strip_last_separator(tmp);
        }

        const char* in = tmp.c_str();
        str_moveable expanded;
        const bool disambiguated = os::disambiguate_abbreviated_path(in, expanded);
        if (expanded.length())
        {
#ifdef DEBUG
            if (dbg_get_env_int("DEBUG_EXPANDABBREV"))
                printf("\x1b[s\x1b[H\x1b[97;48;5;22mEXPANDED:  \"%s\" + \"%s\" (%s)\x1b[m\x1b[K\x1b[u", expanded.c_str(), in, disambiguated ? "UNIQUE" : "ambiguous");
#endif
            if (disambiguated)
            {
                expanded.concat(in);
                assert(in + strlen(in) == tmp.c_str() + tmp.length());
            }

            do_slash_translation(expanded, strpbrk(tmp.c_str(), "/\\"));

            if (!disambiguated)
            {
stop:
                m_buffer->begin_undo_group();
                m_buffer->remove(m_anchor, m_anchor + in - tmp.c_str());
                m_buffer->set_cursor(m_anchor);
                m_buffer->insert(expanded.c_str());
                m_buffer->end_undo_group();
                // Force the menu-complete family of commands to regenerate
                // matches, otherwise they'll have no matches.
                override_rl_last_func(nullptr, true/*force_when_null*/);
                // Since there are no matches, selectcomplete will be canceled
                // after returning.
                return;
            }
            else
            {
                in = tmp.c_str() + tmp.length();
                if (path::is_separator(expanded[expanded.length() - 1]))
                    goto stop;
                tmp = std::move(expanded);
                // Override the input editor's line state info to generate
                // matches using the expanded path, without actually modifying
                // the Readline line buffer (since we're inside a Readline
                // callback and Readline isn't prepared for the buffer to
                // change out from under it).
                needle = tmp.c_str();
                override = true;
            }
        }
    }

    if (m_matches.is_fully_qualify())
    {
        // Note that this can further adjust an expanded abbreviated path
        // using it to override the match line state.
        if (needle != tmp.c_str())
            tmp = needle;
        omls.fully_qualify(m_anchor, m_anchor + m_needle.length(), tmp);
        needle = tmp.c_str();
        override = true;
    }
    else if (override)
    {
        // This applies an expanded abbreviated path.
        omls.override(m_anchor, m_anchor + m_needle.length(), needle);
    }

    // Perform completion again after overriding match line state.
    if (override)
        ::update_matches();

#define m_needle __use_needle_instead__

    // Update Readline modes based on the available completions.
    {
        matches_iter iter = m_matches.get_iter();
        while (iter.next())
            ;
        update_rl_modes_from_matches(m_matches.get_matches(), iter, m_matches.get_match_count());
    }

    // Initialize whether descriptions are available.
    m_matches.init_has_descriptions();

    // Perform match display filtering (match_display_filter or the
    // ondisplaymatches event).
    const display_filter_flags flags = display_filter_flags::selectable;
    if (matches* regen = maybe_regenerate_matches(needle, flags))
    {
        m_matches.set_regen_matches(regen);

        // Build char** array for filtering.
        std::vector<autoptr<char>> matches;
        const uint32 count = m_matches.get_match_count();
        matches.emplace_back(nullptr); // Placeholder for lcd.
        for (uint32 i = 0; i < count; i++)
        {
            const char* text = m_matches.get_match(i);
            const char* disp = m_matches.get_match_display_raw(i);
            const char* desc = m_matches.get_match_description(i);
            const size_t packed_size = calc_packed_size(text, disp, desc);
            char* buffer = static_cast<char*>(malloc(packed_size));
            if (pack_match(buffer, packed_size, text, m_matches.get_match_type(i), disp, desc, m_matches.get_match_append_char(i), m_matches.get_match_flags(i)))
                matches.emplace_back(buffer);
            else
                free(buffer);
        }
        matches.emplace_back(nullptr);

        // Get filtered matches.
        create_matches_lookaside(&*matches.begin());
        m_matches.get_matches()->match_display_filter(needle, &*matches.begin(), &m_data, flags);
        destroy_matches_lookaside(&*matches.begin());

#ifdef DEBUG
        if (dbg_get_env_int("DEBUG_FILTER"))
        {
            puts("-- SELECTCOMPLETE MATCH_DISPLAY_FILTER");
            str<> tmp;
            for (uint32 i = 0; i < m_data.get_match_count(); i++)
            {
                match_type_to_string(m_data.get_match_type(i), tmp);
                printf("type '%s', match '%s', display '%s'\n",
                        tmp.c_str(),
                        m_data.get_match(i),
                        str_or_empty(m_data.get_match_display(i)));
            }
            puts("-- DONE");
        }
#endif

        // Use filtered matches.
        m_matches.set_filtered_matches(&m_data, false/*own*/);
    }
    else
    {
        assert(m_matches.get_matches() == m_init_matches);
        m_data.copy(static_cast<const matches_impl&>(*m_matches.get_matches()));
        m_matches.set_alt_matches(&m_data, false/*own*/);
    }

    // Perform match filtering if needed (the onfiltermatches event).
    m_matches.filter_matches();

#undef m_needle

    // Determine the lcd.
    m_matches.get_lcd(m_needle);
    m_lcd = m_needle.length();

    // Determine the longest match.
    m_match_longest = 0;

    const uint32 count = m_matches.get_match_count();
    for (uint32 i = 0; i < count; i++)
    {
        int32 len = 0;

        match_type type = m_matches.get_match_type(i);
        const char* match = m_matches.get_match(i);
        bool append = m_matches.is_append_display(i);
        if (use_display(append, type, i))
        {
            if (append)
                len += printable_len(match, type);
            len += m_matches.get_match_visible_display(i);
        }
        else
        {
            len += printable_len(match, type);
        }

        if (m_match_longest < len)
            m_match_longest = len;
    }

    m_clear_display = m_any_displayed;
    m_calc_widths = true;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_matches()
{
    if (m_init_matches->is_volatile())
    {
        init_matches();
    }
    else
    {
        match_pipeline pipeline(m_data);
        pipeline.select(m_needle.c_str());
        pipeline.sort();
    }

    m_clear_display = m_any_displayed;
    m_calc_widths = true;
    m_ignore_scroll_offset = false;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_len(uint32 needle_len)
{
    m_len = 0;

    if (m_index < m_matches.get_match_count())
    {
        size_t len = strlen(m_matches.get_match(m_index));
        if (len > needle_len)
            m_len = len - needle_len;
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_layout()
{
#ifdef DEBUG
    m_annotate = !!dbg_get_env_int("DEBUG_SHOWTYPES");
    m_col_extra = m_annotate ? 3 : 0;   // Room for space hex hex.
#endif

    bool init_desc_below = m_calc_widths && m_init_desc_below;
    if (init_desc_below)
    {
        m_init_desc_below = false;
        m_desc_below = false;
        if (m_matches.has_descriptions() && (m_matches.get_match_count() > 100))
        {
force_desc_below:
            m_desc_below = true;
            init_desc_below = false;
        }
    }

    if (m_calc_widths)
    {
#ifdef DEBUG
        const width_t col_extra = m_col_extra;
#else
        const width_t col_extra = 0;
#endif
        const bool best_fit = g_match_best_fit.get();
        const int32 limit_fit = g_match_limit_fitted.get();
        const bool desc_inline = !m_desc_below && m_matches.has_descriptions();
        const bool one_column = desc_inline && m_matches.get_match_count() <= DESC_ONE_COLUMN_THRESHOLD;
        rollback<int32> rcpdl(_rl_completion_prefix_display_length, 0);
        m_widths = calculate_columns(m_matches, best_fit ? limit_fit : -1, one_column, m_desc_below, col_extra);
        m_calc_widths = false;
    }

    const int32 cols_that_fit = m_widths.num_columns();
    m_match_cols = max<int32>(1, cols_that_fit);
    m_match_rows = (m_matches.get_match_count() + (m_match_cols - 1)) / m_match_cols;
    m_ignore_scroll_offset = false;

    // If initializing where to display descriptions, and they don't fit inline
    // in a small number of rows, then display them below.
    if (init_desc_below && !m_desc_below && m_match_rows > DESC_ONE_COLUMN_THRESHOLD)
    {
        m_calc_widths = true;
        goto force_desc_below;
    }

    // +3 for quotes and append character (e.g. space).
    const int32 input_height = (_rl_vis_botlin + 1) + (m_match_longest + 3 + m_screen_cols - 1) / m_screen_cols;
    m_visible_rows = m_screen_rows - input_height;
    m_visible_rows -= min<int32>(2, m_screen_rows / 10);

    // When showing description only for selected item, reserve 2 extra rows for
    // showing the description.
    if (m_desc_below)
        m_visible_rows -= 2;

    // Reserve space for comment row.
    if (m_visible_rows < m_match_rows)
        m_visible_rows--;

    // Finally, limit the rows per the configurable setting.  This happens
    // last so that the number of rows of items is consistent, instead of
    // controlling the overall number of rows for things that may or may not
    // even be present in any given situation.
    const int32 max_rows = g_max_rows.get();
    const int32 min_rows = 2;
    if (max_rows > 0 && m_visible_rows > max(min_rows, max_rows))
        m_visible_rows = max(min_rows, max_rows);

    // At least 2 rows must fit.
    if (m_visible_rows < min_rows)
        m_visible_rows = 0;

#ifdef SHOW_VERT_SCROLLBARS
    m_vert_scroll_car = 0;
    m_vert_scroll_column = 0;
#endif
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_top()
{
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
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_display()
{
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
}

//------------------------------------------------------------------------------
void selectcomplete_impl::insert_needle()
{
    assert(is_active());

    if (m_inserted)
    {
        m_buffer->undo();
        m_inserted = false;
        m_quoted = false;
    }

    m_len = 0;

    const char* match = m_needle.c_str();

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
    m_point = m_buffer->get_cursor();
    m_buffer->insert(qs);
    m_buffer->set_cursor(m_point);
    m_buffer->end_undo_group();
    m_inserted = true;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::insert_match(int32 final)
{
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
}

//------------------------------------------------------------------------------
int32 selectcomplete_impl::get_match_row(int32 index) const
{
    return _rl_print_completions_horizontally ? (index / m_match_cols) : (index % m_match_rows);
}

//------------------------------------------------------------------------------
int32 selectcomplete_impl::get_scroll_offset() const
{
    const int32 ofs = g_clink_scroll_offset.get();
    if (ofs <= 0)
        return 0;
    return min(ofs, max(0, (m_displayed_rows - 1) / 2));
}

//------------------------------------------------------------------------------
bool selectcomplete_impl::use_display(bool append, match_type type, int32 index) const
{
    return m_matches.use_display(index, type, append);
}

//------------------------------------------------------------------------------
void selectcomplete_impl::set_top(int32 top)
{
    assert(top >= 0);
    assert(top <= max<int32>(0, m_match_rows - m_visible_rows));
    if (top != m_top)
    {
        m_top = top;
        m_prev_displayed = -1;
        m_comment_row_displayed = false;
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::reset_top()
{
    m_top = 0;
    m_index = 0;
    m_prev_displayed = -1;
    m_comment_row_displayed = false;
    m_ignore_scroll_offset = false;
}

//------------------------------------------------------------------------------
bool selectcomplete_impl::is_active() const
{
    return m_prev_bind_group >= 0 && m_buffer && m_printer && m_anchor >= 0 && m_point >= m_anchor;
}

//------------------------------------------------------------------------------
bool selectcomplete_impl::accepts_mouse_input(mouse_input_type type) const
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

//------------------------------------------------------------------------------
bool is_select_complete_active()
{
    if (!s_selectcomplete)
        return false;
    return s_selectcomplete->is_active();
}
