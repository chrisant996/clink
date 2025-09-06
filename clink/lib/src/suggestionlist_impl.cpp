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
#include "display_matches.h"
#include "match_colors.h"
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
#include <terminal/terminal_helpers.h>
#include <terminal/wcwidth.h>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
#include <readline/colors.h>
extern int _rl_last_v_pos;
};



//------------------------------------------------------------------------------
static setting_bool s_suggestionlist_default(
    "suggestionlist.default",
    "Start with the suggestion list enabled",
    "When this is true, a Clink session starts with the suggestion list enabled.\n"
    "The suggestion list can be toggled on/off with F2 or whatever key is bound\n"
    "to the clink-toggle-suggestion-list command.",
    false);

static setting_color s_color_suggestionlist(
    "color.suggestionlist",
    "Color for suggestions in the suggestion list",
    "For the selected suggestion, this is added to color.suggestionlist_selected.\n"
    "Typically what works well is having this specify a foreground color.",
    "");

static setting_color s_color_suggestionlist_markup(
    "color.suggestionlist_markup",
    "Color for markup in the suggestion list",
    "For the selected suggestion, this is added to color.suggestionlist_selected.\n"
    "Typically what works well is having this specify a foreground color.",
    "yellow");

static setting_color s_color_suggestionlist_dim(
    "color.suggestionlist_dim",
    "Color for dim text in the suggestion list header",
    "bright black");

static setting_color s_color_suggestionlist_highlight(
    "color.suggestionlist_highlight",
    "Color for highlight in the suggestion list",
    "For the selected suggestion, this is added to color.suggestionlist_selected.\n"
    "Typically what works well is having this specify a foreground color and/or\n"
    "styles such as bold or underline.",
    "bright cyan");

static setting_color s_color_suggestionlist_selected(
    "color.suggestionlist_selected",
    "Color for the current selected suggestion",
    "This is combined with other suggestion list colors.  Typically what works\n"
    "well is having this specify a background color, and having the other\n"
    "suggestion list colors specify a foreground color.",
    "default on bright black");

extern setting_int g_clink_scroll_offset;
extern setting_color g_color_description;



//------------------------------------------------------------------------------
enum {
    bind_id_suggestionlist_up = 60,
    bind_id_suggestionlist_down,
    bind_id_suggestionlist_pgup,
    bind_id_suggestionlist_pgdn,
    bind_id_suggestionlist_leftclick,
    bind_id_suggestionlist_wheelup,
    bind_id_suggestionlist_wheeldown,
    bind_id_suggestionlist_drag,
    bind_id_suggestionlist_escape,

    bind_id_suggestionlist_catchall,
};

const int32 c_max_suggestion_rows = 10;
const int32 c_max_suggestionlist_width = 100;
const char norm[] = "\x1b[m";
const char ital[] = "\x1b[3m";



//------------------------------------------------------------------------------
static bool s_suggestion_list_allowed = false;
static bool s_suggestion_list_default = false;
static bool s_suggestion_list_enabled = false;
static suggestionlist_impl* s_suggestionlist = nullptr;

//------------------------------------------------------------------------------
suggestionlist_impl::suggestionlist_impl(input_dispatcher& dispatcher)
    : m_dispatcher(dispatcher)
{
    if (s_suggestion_list_default != s_suggestionlist_default.get())
    {
        // If the setting's value changed since last we saw it, then update
        // the enabled status so the setting change takes effect immediately.
        s_suggestion_list_default = s_suggestionlist_default.get();
        s_suggestion_list_enabled = s_suggestion_list_default;
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::allow(bool allow)
{
    const bool was_active = is_active();

    m_hide = !allow;

    if (was_active != is_active())
    {
        if (allow)
            update_layout();
        update_display();
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::enable(editor_module::result& result)
{
    s_suggestion_list_enabled = true; // Also disables the comment row.

    clear_suggestion(); // Trigger rerunning suggesters with limit > 1.
    init_suggestions();

    assert(m_any_displayed.empty());
    assert(m_tooltip_displayed < 0);
    assert(!m_clear_display);
    m_any_displayed.clear();
    m_tooltip_displayed = -1;
    m_clear_display = false;

    // Make sure there's room.
    update_layout();

    // Activate key bindings.
    assert(m_prev_bind_group < 0);
    m_prev_bind_group = result.set_bind_group(m_bind_group);

    // Update the display.
    reset_top();
    update_display();
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::toggle(editor_module::result& result)
{
    assert(m_buffer);
    if (!m_buffer)
        return false;

    if (is_active_even_if_hidden())
    {
        cancel(result);
    }
    else
    {
        lock_against_suggestions(false);
        enable(result);
    }
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

    m_bind_group = binder.create_group("suggestionlist");
    binder.bind(m_bind_group, "\\e[A", bind_id_suggestionlist_up);
    binder.bind(m_bind_group, "\\e[B", bind_id_suggestionlist_down);
    binder.bind(m_bind_group, "\\e[5~", bind_id_suggestionlist_pgup);
    binder.bind(m_bind_group, "\\e[6~", bind_id_suggestionlist_pgdn);
    binder.bind(m_bind_group, "\\e[$*;*L", bind_id_suggestionlist_leftclick, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*A", bind_id_suggestionlist_wheelup, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*B", bind_id_suggestionlist_wheeldown, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*;*M", bind_id_suggestionlist_drag, true/*has_params*/);

    binder.bind(m_bind_group, "^g", bind_id_suggestionlist_escape);
    if (esc)
        binder.bind(m_bind_group, esc, bind_id_suggestionlist_escape);

    binder.bind(m_bind_group, "", bind_id_suggestionlist_catchall);
}

//------------------------------------------------------------------------------
static void make_color_sequence(const setting_color& color, str_base& out, int32 reset=0, const char* prefix=nullptr)
{
    str<16> tmp;
    color.get(tmp);

    out.clear();

    const char* keep = tmp.c_str();
    if (reset < 0)
    {
        if (keep[0] == '0')
        {
            if (!keep[1])
                ++keep;
            else if (keep[1] == ';')
                keep += 2;
            else if (keep[1] == '0')
            {
                if (!keep[2])
                    keep += 2;
                else if (keep[2] == ';')
                    keep += 3;
            }
        }
    }

    if (keep[0] || reset > 0)
    {
        out << "\x1b[";
        if (prefix)
            out << prefix << ";";
        else if (reset > 0 && (keep[0] != '0' || keep[1] != ';'))
            out << "0;";
        out << keep;
        out << "m";
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_begin_line(const context& context)
{
    assert(!s_suggestionlist);
    s_suggestionlist = this;
    m_buffer = &context.buffer;
    m_printer = &context.printer;
    m_clear_display = false;
    m_applied = false;
    m_scroll_helper.clear();

    str<16> prefix;
    s_color_suggestionlist.get(prefix);

    make_color_sequence(s_color_suggestionlist, m_list_color, 1);
    make_color_sequence(s_color_suggestionlist_markup, m_markup_color, -1, prefix.c_str());
    make_color_sequence(s_color_suggestionlist_markup, m_header_markup_color, 1);
    make_color_sequence(s_color_suggestionlist_dim, m_dim_color, 1);
    make_color_sequence(s_color_suggestionlist_highlight, m_highlight_color, -1, prefix.c_str());
    make_color_sequence(s_color_suggestionlist_selected, m_selected_color, -1);
    make_color_sequence(g_color_description, m_tooltip_color);

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
    m_applied = false;
    m_ignore_scroll_offset = false;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_need_input(int32& bind_group)
{
    if (is_select_complete_active())
        return;


    if (m_index >= 0 && !is_locked_against_suggestions())
        clear_index();

    allow_suggestion_list(1);

    if (m_first_input)
    {
        // At the beginning of a new input line, maybe automatically enable
        // the suggestion list.  This applies the suggestionlist.default
        // setting, as well as carrying over the suggestion list mode from
        // the previous input line.
        m_first_input = false;
        assert(m_buffer);
        assert(m_printer);
        assert(m_bind_group >= 0);
        if (s_suggestion_list_enabled && m_prev_bind_group < 0 &&
            bind_group >= 0 && bind_group != m_bind_group &&
            !rl_has_saved_history())
        {
            class result_shim : public editor_module::result
            {
            public:
                                result_shim(int32& bind_group) : m_bind_group(bind_group) {}
                virtual void    pass() override { assert(false); }
                virtual void    loop() override { assert(false); }
                virtual void    done(bool eof) override { assert(false); }
                virtual void    redraw() override { assert(false); }
                virtual int32   set_bind_group(int32 id) override { int32 t = m_bind_group; m_bind_group = id; return t; }
            private:
                int32&          m_bind_group;
            };

            result_shim result(bind_group);
            enable(result);
        }
    }

    if (is_active())
        bind_group = m_bind_group;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::on_input(const input& _input, result& result, const context& context)
{
    assert(is_active());

    input input = _input;

    if (m_visible_rows <= 0)
        goto catchall;

    m_ignore_scroll_offset = false;

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
            const int32 y = m_index;
            const int32 rows = min<int32>(m_count, m_visible_rows);
            const int32 scroll_ofs = get_scroll_offset();
            // Use rows as the page size (vs the more common rows-1) for
            // compatibility with Conhost's F7 popup list behavior.
            const int32 scroll_rows = (rows - scroll_ofs);
            if (input.id == bind_id_suggestionlist_pgup)
            {
                if (y > 0)
                {
                    int32 new_y = max<int32>(0, (y <= m_top + scroll_ofs) ? y - scroll_rows : m_top + scroll_ofs);
                    m_index += (new_y - y);
                    goto navigated;
                }
            }
            else if (input.id == bind_id_suggestionlist_pgdn)
            {
                if (y < m_count - 1)
                {
                    int32 bottom_y = m_top + scroll_rows - 1;
                    int32 new_y = min<int32>(m_count - 1, (y == bottom_y) ? y + scroll_rows : bottom_y);
                    m_index += (new_y - y);
                    if (m_index > m_count - 1)
                    {
                        set_top(max<int32>(0, m_count - m_visible_rows));
                        m_index = m_count - 1;
                    }
                    goto navigated;
                }
            }
        }
        break;

    case bind_id_suggestionlist_leftclick:
    case bind_id_suggestionlist_drag:
        {
            const uint32 now = m_scroll_helper.on_input();

            uint32 p0, p1;
            input.params.get(0, p0);
            input.params.get(1, p1);
            p1 -= m_mouse_offset;
            const uint32 rows = m_displayed_rows;

#ifdef SHOW_VERT_SCROLLBARS
            if (m_vert_scroll_car && input.id == bind_id_suggestionlist_leftclick)
                m_scroll_bar_clicked = (p0 == m_vert_scroll_column && p1 >= 0 && p1 < rows);

            if (m_scroll_bar_clicked)
            {
                const int32 row = min<int32>(max<int32>(p1, 0), rows - 1);
                m_index = hittest_scroll_car(row, rows, m_count);
                set_top(min<int32>(max<int32>(m_index - (rows / 2), 0), m_count - rows));
                apply_suggestion(m_index);
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
                int32 index = row;
                const uint32 x1 = 0;
                const uint32 col_width = m_max_width;
                if (p0 >= x1 && p0 < x1 + col_width)
                {
                    m_index = index;
                    m_ignore_scroll_offset = true;
                    if (scrolling)
                        m_scroll_helper.on_scroll(now);
                    if (m_index >= m_count)
                    {
                        set_top(max<int32>(revert_top, m_count - (rows - 1)));
                        m_index = m_count - 1;
                    }
                    apply_suggestion(m_index);
                    update_display();
                    scrolling = false; // Don't revert top.
                    break;
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
                    // Let something else process the click.
                    goto catchall;
                }
            }
            else if (input.id == bind_id_suggestionlist_drag)
            {
                if (m_scroll_helper.can_scroll() && m_top + rows < m_count)
                {
                    row = m_top + rows;
                    set_top(min<int32>(m_count - rows, m_top + m_scroll_helper.scroll_speed()));
                    scrolling = true;
                    goto do_mouse_position;
                }
            }
        }
        break;

    case bind_id_suggestionlist_wheelup:
    case bind_id_suggestionlist_wheeldown:
        {
            uint32 p0;
            input.params.get(0, p0);
            const int32 y = m_index;
            const int32 prev_index = m_index;
            const int32 prev_top = m_top;
            if (input.id == bind_id_suggestionlist_wheelup)
                m_index -= min<uint32>(y, p0);
            else
                m_index += min<uint32>(m_count - 1 - y, p0);

            const int32 count = m_count;
            if (m_index >= count)
            {
                m_index = count - 1;
                const int32 rows = min<int32>(count, m_visible_rows);
                if (m_top + rows - 1 == m_index)
                {
                    const int32 max_top = max<int32>(0, m_count - rows);
                    set_top(min<int32>(max_top, m_top + 1));
                }
            }
            assert(!m_ignore_scroll_offset);

            if (m_index != prev_index || m_top != prev_top)
                apply_suggestion(m_index);

            if (m_index != prev_index || m_top != prev_top)
                update_display();
        }
        break;

    case bind_id_suggestionlist_escape:
        // Hide suggestion list until the input line is changed by something
        // other than the suggestion list.
        allow_suggestion_list(0);
        result.set_bind_group(m_prev_bind_group);
        suppress_suggestions();
        m_suggestions.clear();
        m_count = 0;
        assert(!is_active());
        update_layout();
        update_display();
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
void suggestionlist_impl::cancel(editor_module::result& result)
{
    assert(is_active_even_if_hidden());

    if (m_applied && is_locked_against_suggestions())
        m_buffer->undo();
    m_applied = false;
    lock_against_suggestions(false);

    result.set_bind_group(m_prev_bind_group);
    m_prev_bind_group = -1;

    update_display();

    s_suggestion_list_enabled = false; // Also re-enables the comment row.
}

//------------------------------------------------------------------------------
void suggestionlist_impl::init_suggestions()
{
    // Don't update suggestions if the cursor isn't at the end of the line.
    // There will be no suggestions, so the suggestion list would disappear,
    // which isn't the intended behavior for the suggestion list.
    if (m_buffer->get_cursor() < m_buffer->get_length())
        return;

    const auto& id = m_suggestions.get_generation_id();

    get_suggestions(m_suggestions);

    if (id != m_suggestions.get_generation_id())
    {
        m_index = -1;
        m_prev_displayed = -1;
        m_count = m_suggestions.size();
        m_clear_display = !m_any_displayed.empty();
        m_ignore_scroll_offset = false;
        m_max_rows = 0;
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_layout(bool refreshing_display)
{
    // TODO: this kind of has to hide the comment row, but then that disables
    // features like input hints...

    const int32 input_height = (_rl_vis_botlin + 1);
    const int32 header_row = 1;
    const int32 tooltip_row = 1;
    int32 available_rows = m_screen_rows - input_height - header_row - tooltip_row;
    available_rows = min<>(available_rows, m_screen_rows / 2);
    m_visible_rows = min<>(c_max_suggestion_rows, m_count);

    // At least 3 rows must fit.
    if (m_visible_rows > available_rows && available_rows < 3)
        m_visible_rows = 0;
    m_visible_rows = min<>(m_visible_rows, available_rows);

    if (!refreshing_display)
    {
        m_ignore_scroll_offset = false;

#ifdef SHOW_VERT_SCROLLBARS
        m_vert_scroll_car = 0;
        m_vert_scroll_column = 0;
#endif
    }

#ifdef SHOW_VERT_SCROLLBARS
    const int32 reserve_cols = 3;
#else
    const int32 reserve_cols = 1;
#endif
    m_max_width = min<>(m_screen_cols - reserve_cols, c_max_suggestionlist_width);

    // If the number of visible rows shrinks, then make that the new max
    // number of visible rows.  This happens when applying the selected
    // suggestion makes the input line so large that it reduces how much room
    // is left over below it for the suggestion list.
    if (m_max_rows)
        m_visible_rows = min<>(m_visible_rows, m_max_rows);
    m_max_rows = m_visible_rows;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_top()
{
    int32 y = m_index;
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

    if (!m_ignore_scroll_offset)
    {
        const int32 scroll_ofs = get_scroll_offset();
        if (scroll_ofs > 0)
        {
            y = m_index;
            const int32 last_row = max<int32>(0, m_count - m_visible_rows);
            if (m_top > max(0, y - scroll_ofs))
                set_top(max(0, y - scroll_ofs));
            else if (m_top < min(last_row, y + scroll_ofs - m_displayed_rows + 1))
                set_top(min(last_row, y + scroll_ofs - m_displayed_rows + 1));
        }
    }

    assert(m_top >= 0);
    assert(m_top <= max<int32>(0, m_count - m_visible_rows));
}

//------------------------------------------------------------------------------
void suggestionlist_impl::update_display()
{
    // No-op if there are no visible rows and nothing needs to be erased.
    if (m_visible_rows <= 0 && m_any_displayed.empty())
    {
        m_clear_display = false;
        return;
    }

    // No-op if the selected item hasn't changed, unless the list is not
    // active and m_any_displayed is not empty.  All other no-op cases set
    // m_prev_displayed to -1 to force updating the display.
    if (m_prev_displayed >= 0 && m_prev_displayed == m_index &&
        !(!is_active() && !m_any_displayed.empty()) && !m_clear_display)
    {
#ifdef DEBUG
        str<> value;
        if (!os::get_env("DEBUG_NO_SUGGESTIONLIST_DISPLAY_OPTIMIZATION", value) || !atoi(value.c_str()))
#endif
            return;
    }

#ifdef SHOW_VERT_SCROLLBARS
    m_vert_scroll_car = 0;
    m_vert_scroll_column = 0;
#endif

    // Hide cursor.
    const bool was_visible = show_cursor(false);

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

    // Display suggestions.
    int32 up = 0;
    if (is_active() && m_count > 0)
    {
        str_moveable tmp;
        const int32 rows = min<>(m_visible_rows, m_count);
        m_displayed_rows = rows;

        rl_crlf();
        up++;

        const bool clear_display = m_clear_display;
        if (m_clear_display)
        {
            m_prev_displayed = -1;
            m_tooltip_displayed = -1;
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
        left.format("%s%s<%s/%u>%s", m_header_markup_color.c_str(), ital, num.c_str(), m_count, norm);
        const int32 left_header_cells = cell_count(left.c_str());
        if (m_max_width > left_header_cells + 2) // At least 2 spaces after.
            make_sources_header(right, m_max_width - (left_header_cells + 2));

        // Show the header row.
        {
            const int32 right_header_cells = cell_count(right.c_str());
            if (m_max_width > (left_header_cells + right_header_cells))
            {
                const int32 spaces = m_max_width - (left_header_cells + right_header_cells);
                concat_spaces(tmp, spaces);
            }
        }
        m_printer->print(left.c_str(), left.length());
        m_printer->print(tmp.c_str(), tmp.length());
        m_printer->print(right.c_str(), right.length());
        m_printer->print("\x1b[m\x1b[K");

        // Can't update top until after m_displayed_rows is known, so that the
        // scroll offset can be accounted for accurately in all cases.
        update_top();

#ifdef SHOW_VERT_SCROLLBARS
        m_vert_scroll_car = (use_vert_scrollbars() && m_screen_cols >= 8) ? calc_scroll_car_size(rows, m_count) : 0;
        if (m_vert_scroll_car)
            m_vert_scroll_column = m_max_width + 2 - 1;
        const int32 car_top = calc_scroll_car_offset(m_top, rows, m_count, m_vert_scroll_car);
#endif

        int32 shown = 0;
        int32 tooltip = -1;
        const int32 was_tooltip = m_tooltip_displayed;
        int32 screen_row = 0;
        for (int32 row = 0; row < rows; ++row, ++screen_row)
        {
            const int32 i = (m_top + row);
            if (i >= m_count)
                break;

            rl_crlf();
            ++up;

            // Print entry.
            const auto& s = m_suggestions[i];
            if (m_prev_displayed < 0 ||
                i == m_index ||
                i == m_prev_displayed ||
                screen_row >= m_any_displayed.size() ||
                m_any_displayed[screen_row] != i)
            {
                const bool selected = (i == m_index);
                const char* const selected_color = selected ? m_selected_color.c_str() : "";

                if (screen_row >= m_any_displayed.size())
                    m_any_displayed.emplace_back(i);
                else
                    m_any_displayed[screen_row] = i;
                assert(m_any_displayed.size() >= screen_row);

                left.format("%s%s>%s%s ", m_markup_color.c_str(), selected_color, m_list_color.c_str(), selected_color);
                right.format("[%s%s%s%s%s]", m_markup_color.c_str(), selected_color, s.m_source.c_str(), m_list_color.c_str(), selected_color);
                const uint32 used_width = cell_count(left.c_str()) + cell_count(right.c_str());
                if (used_width < m_max_width)
                    make_suggestion_list_string(i, tmp, m_max_width - used_width);
                else
                    tmp.clear();
                m_printer->print(left.c_str(), left.length());
                m_printer->print(tmp.c_str(), tmp.length());
                m_printer->print(right.c_str(), right.length());

#ifdef SHOW_VERT_SCROLLBARS
                draw_scrollbar_char(screen_row, car_top);
#endif // SHOW_VERT_SCROLLBARS

                // Clear to end of line.
                m_printer->print("\x1b[m\x1b[K");

                // Draw or remove tooltip if needed.
                if (selected)
                {
                    if (!s.m_tooltip.empty())
                    {
                        // When inserting a tooltip, must redraw subsequent rows.
                        if (was_tooltip < 0)
                            m_prev_displayed = -1;

                        ++screen_row;
                        if (screen_row >= m_any_displayed.size())
                            m_any_displayed.emplace_back(-1);
                        else
                            m_any_displayed[screen_row] = -1;
                        assert(m_any_displayed.size() >= screen_row);

                        tooltip = m_index;
                        rl_crlf();
                        ++up;
                        const int32 indent_width = 4;
                        tmp.clear();
                        concat_spaces(tmp, indent_width);
                        tmp << m_tooltip_color;
                        m_printer->print(tmp.c_str(), tmp.length());
                        const int32 tooltip_width = ellipsify(s.m_tooltip.c_str(), m_max_width - indent_width, tmp, false);
                        tmp << norm;
                        const int32 spaces = m_max_width - (indent_width + tooltip_width);
                        if (spaces > 0)
                            concat_spaces(tmp, spaces);
                        m_printer->print(tmp.c_str(), tmp.length());
#ifdef SHOW_VERT_SCROLLBARS
                        draw_scrollbar_char(screen_row, car_top);
#endif // SHOW_VERT_SCROLLBARS
                        m_printer->print("\x1b[m\x1b[K");
                    }
                    else
                    {
                        // When removing a tooltip, must redraw subsequent rows.
                        if (was_tooltip >= 0)
                            m_prev_displayed = -1;
                    }
                }
            }
        }

        if (clear_display || (was_tooltip >= 0 && tooltip < 0))
            m_printer->print("\x1b[m\x1b[J");

        assert(!m_clear_display);
        m_prev_displayed = m_index;
        assert(m_any_displayed.size() >= screen_row);
        if (m_any_displayed.size() > screen_row)
            m_any_displayed.resize(screen_row);
        m_tooltip_displayed = tooltip;
    }
    else
    {
        if (!m_any_displayed.empty())
        {
            // Move cursor to next line, then clear to end of screen.
            rl_crlf();
            up++;
            m_printer->print("\x1b[m\x1b[J");
        }
        m_prev_displayed = -1;
        m_any_displayed.clear();
        m_tooltip_displayed = -1;
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
    m_mouse_offset = csbi.dwCursorPosition.Y + 2/*to top item*/;
    _rl_move_vert(vpos);
    _rl_last_c_pos = cpos;
    GetConsoleScreenBufferInfo(h, &csbi);
    restore.Y = csbi.dwCursorPosition.Y;
    SetConsoleCursorPosition(h, restore);

    // Restore cursor.
    show_cursor(was_visible);
}

//------------------------------------------------------------------------------
#ifdef SHOW_VERT_SCROLLBARS
void suggestionlist_impl::draw_scrollbar_char(int32 row, int32 car_top)
{
    if (m_vert_scroll_car)
    {
#ifdef USE_FULL_SCROLLBAR
        constexpr bool floating = false;
#else
        constexpr bool floating = true;
#endif
        str<16> tmp;
        const char* car = get_scroll_car_char(row, car_top, m_vert_scroll_car, floating);
        if (car)
        {
            // Space was reserved by update_layout().
            tmp.format("%s \x1b[0;90m%s", norm, car);
            m_printer->print(tmp.c_str(), tmp.length());
        }
#ifdef USE_FULL_SCROLLBAR
        else
        {
            // Space was reserved by update_layout().
            tmp.format("%s \x1b[0;90m\xe2\x94\x82", norm);// │
            m_printer->print(tmp.c_str(), tmp.length());
        }
#endif
    }
}
#endif // SHOW_VERT_SCROLLBARS

//------------------------------------------------------------------------------
void suggestionlist_impl::make_sources_header(str_base& out, uint32 max_width)
{
    // Ensure room for the "<" and ">" ends.
    if (max_width <= 2)
        return;
    max_width -= 2;

    struct source_group
    {
        const char* m_source;
        str<32> m_caption;
        uint32 m_count = 0;
        uint32 m_width = 0;
        bool m_gray = true;
    };

    const char* source = nullptr;
    std::vector<source_group> groups;
    for (size_t index = 0; index < m_suggestions.size(); ++index)
    {
        auto& s = m_suggestions[index].m_source;
        if (!source || !s.equals(source))
        {
            groups.emplace_back();
            auto& back = groups.back();
            back.m_source = s.c_str();
            source = s.c_str();
        }
        auto& back = groups.back();
        ++back.m_count;
    }

    str<128> tmp;
    int32 num = 0;
    uint32 total_width = 0;
    const uint32 max_width_for_one = max_width * 2 / 3;
    for (auto& group : groups)
    {
        if (num <= m_index && m_index < num + group.m_count)
            tmp.format("%s(%u/%u)", group.m_source, m_index - num + 1, group.m_count);
        else
            tmp.format("%s(%u)", group.m_source, group.m_count);
        group.m_width = ellipsify_ex(tmp.c_str(), max_width_for_one, ellipsify_mode::LEFT, group.m_caption, nullptr, true/*expand_ctrl*/);
        total_width += !!num + group.m_width;
        group.m_gray = !(num <= m_index && m_index < num + group.m_count);
        num += group.m_count;
    }

    bool gray = false;
    uint32 pre_width = 0;   // Width before current group.
    uint32 curr_width = 0;  // Width of current group.
    tmp.clear();
    for (auto& group : groups)
    {
        const bool was_empty = tmp.empty();

        if (!gray && group.m_gray)
        {
            gray = true;
            tmp << m_dim_color;
            tmp << ital;
        }

        if (!was_empty)
            tmp << " ";

        if (!group.m_gray)
        {
            pre_width = cell_count(tmp.c_str());
            gray = false;
            tmp << m_header_markup_color;
            tmp << ital;
        }

        tmp << group.m_caption;

        if (!group.m_gray)
            curr_width = group.m_width;
    }

    if (total_width > max_width)
    {
        const uint32 min_width_pre = (max_width - curr_width) / 2;
        const uint32 min_width_post = (max_width - curr_width) - min_width_pre;
        uint32 post_width = total_width - curr_width - pre_width;
        uint32 keep_width = max_width - curr_width;
        uint32 pre_keep = min<>(pre_width, min_width_pre);
        uint32 post_keep = min<>(post_width, min_width_post);
        if (pre_keep < min_width_pre)
            post_keep += (min_width_pre - pre_keep);
        if (post_keep < min_width_post)
            pre_keep += (min_width_post - post_keep);
        assert(pre_keep + curr_width + post_keep == max_width);

        str_moveable tmp2;
        ellipsify_ex(tmp.c_str(), total_width - (pre_width - pre_keep), ellipsify_mode::LEFT, tmp2);
        ellipsify_ex(tmp2.c_str(), total_width - (pre_width - pre_keep) - (post_width - post_keep), ellipsify_mode::RIGHT, tmp);
    }

    out.format("%s%s<%s%s%s>", m_dim_color.c_str(), ital, tmp.c_str(), m_dim_color.c_str(), ital);
}

//------------------------------------------------------------------------------
static void concat_expandctrl_adjust_highlight(str_base& out, const char* text, int32 len, int32& hs, int32& he)
{
    for (const char* walk = text; len && *walk; ++walk, --len)
    {
        if (CTRL_CHAR(*walk))
        {
            char ctrl[3] = { '^', char(UNCTRL(*walk)), 0 };
            if (hs > out.length())
                ++hs;
            if (he > out.length())
                ++he;
            out.concat(ctrl, 2);
        }
        else
        {
            out.concat(walk, 1);
        }
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::make_suggestion_list_string(int32 index, str_base& out, uint32 width)
{
    const auto& s = m_suggestions[index];
    const bool selected = (index == m_index);
    const char* const selected_color = selected ? m_selected_color.c_str() : nullptr;
    int32 match_offset = max<int32>(0, min<int32>(s.m_suggestion_offset, m_suggestions.get_line().length()));
    int32 suggestion_len = s.m_suggestion.length();

    str<128> whole;
    str<128> tmp2;

    int32 hs = s.m_highlight_offset;
    int32 he = s.m_highlight_offset + s.m_highlight_length;

    // Build the whole line.
    concat_expandctrl_adjust_highlight(whole, m_suggestions.get_line().c_str(), match_offset, hs, he);
    match_offset = whole.length();
    const uint32 pre_cells = clink_wcswidth(whole.c_str(), match_offset);
    concat_expandctrl_adjust_highlight(whole, s.m_suggestion.c_str(), s.m_suggestion.length(), hs, he);
    const uint32 suggestion_cells = clink_wcswidth(whole.c_str() + match_offset, whole.length() - match_offset);
    hs = min<int32>(hs, whole.length());
    he = min<int32>(he, whole.length());

    // Find the highlight inside it.
    uint32 pre_highlight = hs >= 0 ? clink_wcswidth_expandctrl(whole.c_str(), hs) : 0;
    uint32 post_highlight = hs >= 0 ? clink_wcswidth_expandctrl(whole.c_str() + hs, whole.length() - hs) : 0;

    // Add highlight colors.
    const char* after_highlight = whole.c_str();
    if (hs >= 0 && he > hs)
    {
        int32 adjust_match_offset = 0;
        int32 adjust_suggestion_len = 0;
        tmp2.concat(whole.c_str(), hs);
        {
            const str_base& color = m_highlight_color;
            const uint32 color_len = m_highlight_color.length() + (selected_color ? str_len(selected_color) : 0);
            tmp2.concat(color.c_str());
            if (selected_color)
                tmp2.concat(selected_color);
            if (hs < match_offset)
                adjust_match_offset += color_len;
            else
                adjust_suggestion_len += color_len;
        }
        tmp2.concat(whole.c_str() + hs, he - hs);
        {
            const str_base& color = m_list_color;
            const uint32 color_len = m_list_color.length() + (selected_color ? str_len(selected_color) : 0);
            tmp2.concat(color.c_str());
            if (selected_color)
                tmp2.concat(selected_color);
            if (he <= match_offset)
                adjust_match_offset += color_len;
            else
                adjust_suggestion_len += color_len;
        }
        tmp2.concat(whole.c_str() + he);
        after_highlight = tmp2.c_str();
        match_offset += adjust_match_offset;
        suggestion_len += adjust_suggestion_len;
    }

    // Show as much of the suggestion as possible, with at least 1/8 of the
    // available width as chars of context to the left of the start of the
    // suggestion.  This relies on ellipsify_ex both for truncation and also
    // for expanding control characters.
    str<128> tmp3;
    str<128> tmp4;
    const char* result = after_highlight;
    int32 cells = pre_cells + suggestion_cells;
    if (cells >= width && hs >= 0)
    {
        if (int32(width) - int32(post_highlight + 1) > int32(width) / 8)
            pre_highlight = min<>(pre_highlight, width - (post_highlight + 1));
        else
            pre_highlight = min<>(pre_highlight, width / 8);
        const int32 width_after_trim1 = ellipsify_ex(after_highlight, pre_highlight + post_highlight, ellipsify_mode::LEFT, tmp3, nullptr, true/*expand_ctrl*/);
        const int32 len_of_trim1 = tmp3.length() - (str_len(after_highlight) - hs);
        assert(cell_count(tmp3.c_str(), len_of_trim1) == width_after_trim1 - post_highlight);
        cells = ellipsify_ex(tmp3.c_str(), width - 1, ellipsify_mode::RIGHT, tmp4, nullptr, true/*expand_ctrl*/);
        assert(cells <= width);
        assert(cells == cell_count(tmp4.c_str()));
        result = tmp4.c_str();
    }

    // Copy into out buffer.
    out = result;

    // Pad with spaces to the specified width.
    if (cells < width)
    {
        out.concat(m_list_color.c_str());
        if (selected_color)
            out.concat(selected_color);
        concat_spaces(out, width - cells);
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::apply_suggestion(int32 index)
{
    assert(is_active());

    if (m_applied && is_locked_against_suggestions())
    {
        m_buffer->undo();
        assert(m_applied);
        assert(!is_locked_against_suggestions());
    }

    const int32 old_botlin = _rl_vis_botlin;

    if (index >= 0 && index < m_suggestions.size())
    {
        const suggestion& suggestion = m_suggestions[index];
        m_buffer->begin_undo_group();
        m_buffer->remove(suggestion.m_suggestion_offset, m_buffer->get_length());
        m_buffer->set_cursor(suggestion.m_suggestion_offset);
        m_buffer->insert(suggestion.m_suggestion.c_str());
        m_buffer->end_undo_group();

        m_applied = true;
        lock_against_suggestions(true);
    }

    m_buffer->draw();

    // NOTE:  This doesn't need to clear the screen or update layout and
    // display when _rl_vis_botlin changes, because display_manager (inside
    // the draw() call above) calls update_suggestion_list to redisplay it.
}

//------------------------------------------------------------------------------
int32 suggestionlist_impl::get_scroll_offset() const
{
    const int32 ofs = g_clink_scroll_offset.get();
    if (ofs <= 0)
        return 0;
    return min(ofs, max(0, (m_visible_rows - 1) / 2));
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
        // IMPORTANT:  Do not clear m_tooltip_displayed here; update_display()
        // still needs to know whether it was set so it knows whether to clear
        // the display at the end to erase lingering rows.
    }
}

//------------------------------------------------------------------------------
void suggestionlist_impl::reset_top()
{
    m_top = -1;
    set_top(0);
    m_ignore_scroll_offset = false;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::clear_index(bool force)
{
    if (force || is_locked_against_suggestions())
    {
        m_index = -1;
        m_applied = false;
        lock_against_suggestions(false);
        update_layout();
        update_display();
    }
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::get_selected_history_index(int32& index) const
{
    if (!is_active() || m_index < 0)
    {
        index = -1;
        return false;
    }

    index = m_suggestions[m_index].m_history_index;
    return true;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::remove_history_index(int32 history_index)
{
    if (!is_active() || m_index < 0)
        return false;

    if (m_suggestions[m_index].m_history_index != history_index)
        return false;

    m_suggestions.remove_if_history_index(history_index);
    m_count = m_suggestions.size();
    if (m_index >= m_count)
        m_index = m_count - 1;

    // Set m_clear_display before calling apply_suggestion so it can do the
    // clear if necessary.  That lets the subsequent update_display call turn
    // into a no-op in that case.
    m_clear_display = true;

    apply_suggestion(m_index);

    update_layout();
    update_display();
    return true;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::is_active() const
{
    return m_prev_bind_group >= 0 && m_buffer && m_printer && !m_hide;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::is_active_even_if_hidden() const
{
    return m_prev_bind_group >= 0 && m_buffer && m_printer;
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::test_frozen()
{
    if (m_index < 0 || !m_buffer)
        return false;
    if (is_locked_against_suggestions())
        return true;
    clear_index(true/*force*/);
    return false;
}

//------------------------------------------------------------------------------
void suggestionlist_impl::refresh_display(bool clear)
{
    if (!is_active())
        return;

    if (clear)
        m_clear_display = true;

    // This sounds like it would be too expensive to happen every time the
    // input line is displayed.  But it's optimized to be a no-op if the
    // suggestions haven't changed.  So it ends up nicely encapsulating the
    // details so callers don't have to know anything.
    init_suggestions();

    update_layout(true/*refreshing_display*/);
    update_display();
}

//------------------------------------------------------------------------------
bool suggestionlist_impl::accepts_mouse_input(mouse_input_type type) const
{
    switch (type)
    {
    case mouse_input_type::left_click:
    case mouse_input_type::wheel:
    case mouse_input_type::drag:
        return is_active() && m_count > 0;
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
extern "C" void allow_suggestion_list(int allow)
{
    if (s_suggestion_list_allowed == !!allow)
        return;

    s_suggestion_list_allowed = !!allow;

    if (s_suggestionlist && !!allow != s_suggestionlist->is_active())
        s_suggestionlist->allow(allow);
}

//------------------------------------------------------------------------------
extern "C" void clear_suggestion_list_index(void)
{
    if (!s_suggestionlist)
        return;

    s_suggestionlist->clear_index();
}

//------------------------------------------------------------------------------
extern "C" int get_suggestion_list_selected_history_index(int* index)
{
    if (!s_suggestionlist)
    {
        *index = -1;
        return false;
    }

    return s_suggestionlist->get_selected_history_index(*index);
}

//------------------------------------------------------------------------------
bool remove_suggestion_list_history_index(int32 rl_history_index)
{
    if (!s_suggestionlist)
        return false;

    return s_suggestionlist->remove_history_index(rl_history_index);
}

//------------------------------------------------------------------------------
bool is_suggestion_list_enabled()
{
    return s_suggestion_list_enabled;
}

//------------------------------------------------------------------------------
bool is_suggestion_list_active(bool even_if_hidden)
{
    if (!s_suggestionlist)
        return false;

    return even_if_hidden ?
        s_suggestionlist->is_active_even_if_hidden() :
        s_suggestionlist->is_active();
}

//------------------------------------------------------------------------------
bool test_suggestion_list_frozen()
{
    if (!s_suggestionlist)
        return false;

    return s_suggestionlist->test_frozen();
}

//------------------------------------------------------------------------------
void update_suggestion_list_display(bool clear)
{
    if (!s_suggestionlist)
        return;

    s_suggestionlist->refresh_display(clear);
}

