// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "textlist_impl.h"
#include "binder.h"
#include "editor_module.h"
#include "bind_resolver.h"
#include "line_buffer.h"
#include "ellipsify.h"
#include "clink_ctrlevent.h"
#include "clink_rl_signal.h"
#include "history_timeformatter.h"
#include "line_editor_integration.h"
#ifdef SHOW_VERT_SCROLLBARS
#include "scroll_car.h"
#endif

#include <core/base.h>
#include <core/settings.h>
#include <core/str_compare.h>
#include <core/str_iter.h>
#include <core/debugheap.h>
#include <rl/rl_commands.h>
#include <terminal/printer.h>
#include <terminal/ecma48_iter.h>
#include <terminal/wcwidth.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
#include <terminal/terminal_helpers.h>
#include <terminal/key_tester.h>
#include <signal.h>
#include <shellapi.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/history.h>
extern int _rl_last_v_pos;
};



//------------------------------------------------------------------------------
enum {
    bind_id_textlist_up = 60,
    bind_id_textlist_down,
    bind_id_textlist_left,
    bind_id_textlist_right,
    bind_id_textlist_ctrlleft,
    bind_id_textlist_ctrlright,
    bind_id_textlist_pgup,
    bind_id_textlist_pgdn,
    bind_id_textlist_home,
    bind_id_textlist_end,
    bind_id_textlist_ctrlhome,
    bind_id_textlist_ctrlend,
    bind_id_textlist_findincr,
    bind_id_textlist_findnext,
    bind_id_textlist_findprev,
    bind_id_textlist_copy,
    bind_id_textlist_backspace,
    bind_id_textlist_delete,
    bind_id_textlist_escape,
    bind_id_textlist_enter,
    bind_id_textlist_insert,
    bind_id_textlist_leftclick,
    bind_id_textlist_doubleclick,
    bind_id_textlist_wheelup,
    bind_id_textlist_wheeldown,
    bind_id_textlist_drag,
    bind_id_textlist_togglefilter,
    bind_id_textlist_help,

    bind_id_textlist_catchall = binder::id_catchall_only_printable,
};

//------------------------------------------------------------------------------
static setting_enum g_popup_search_mode(
    "clink.popup_search_mode",
    "Default search mode in popup lists",
    "When this is 'find', typing in popup lists moves to the next matching item.\n"
    "When this is 'filter', typing in popup lists filters the list.",
    "find,filter",
    0);

extern setting_enum g_ignore_case;
extern setting_bool g_fuzzy_accent;
extern setting_enum g_history_timestamp;
extern bool host_remove_dir_history(int32 index);

//------------------------------------------------------------------------------
static textlist_impl* s_textlist = nullptr;
static bool s_standalone = false;
static int32 s_old_default_popup_search_mode = -1;
static int32 s_default_popup_search_mode = -1;
const int32 min_screen_cols = 20;

//------------------------------------------------------------------------------
static int32 make_item(const char* in, str_base& out)
{
    out.clear();

    int32 cells = 0;
    for (wcwidth_iter iter(in); int32 c = iter.next();)
    {
        if (iter.character_wcwidth_signed() < 0)
        {
            char ctrl[2];
            ctrl[0] = '^';
            ctrl[1] = CTRL_CHAR(c) ? UNCTRL(c) : '?';
            out.concat(ctrl, 2);
            cells += 2;
        }
        else
        {
            out.concat(iter.character_pointer(), iter.character_length());
            cells += iter.character_wcwidth_signed();
        }
    }

    return cells;
}

//------------------------------------------------------------------------------
static int32 make_column(const char* in, const char* end, str_base& out)
{
    out.clear();

    int32 cells = 0;

    ecma48_state state;
    ecma48_iter iter(in, state, end ? int32(end - in) : -1);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            for (wcwidth_iter inner_iter(code.get_pointer(), code.get_length());
                 int32 c = inner_iter.next();
                 )
            {
                if (c == '\r' || c == '\n')
                {
                    out.concat(" ", 1);
                    cells++;
                }
                else if (inner_iter.character_wcwidth_signed() < 0)
                {
                    char ctrl[2];
                    ctrl[0] = '^';
                    ctrl[1] = CTRL_CHAR(c) ? UNCTRL(c) : '?';
                    out.concat(ctrl, 2);
                    cells += 2;
                }
                else
                {
                    out.concat(inner_iter.character_pointer(), inner_iter.character_length());
                    cells += inner_iter.character_wcwidth_signed();
                }
            }
        }

    return cells;
}

//------------------------------------------------------------------------------
static int32 limit_cells(const char* in, int32 limit, int32& cells, int32 horz_offset=0, str_base* out=nullptr, const char** text_ptr=nullptr)
{
    if (out)
        out->clear();
    if (text_ptr)
        *text_ptr = nullptr;

    cells = 0;
    wcwidth_iter iter(in, strlen(in));

    if (horz_offset)
    {
        int32 skip = horz_offset;
        const char* const orig = in;
        while (skip > 0 && iter.next())
        {
            const int32 width = iter.character_wcwidth_onectrl();
            if (width > 0)
            {
                skip -= width;
                in += iter.character_length();
            }
        }

        if (out)
        {
            out->concat(ellipsis, ellipsis_len);
            cells += ellipsis_cells;
            for (skip = ellipsis_cells; skip > 0 && iter.next();)
            {
                const int32 width = iter.character_wcwidth_onectrl();
                if (width > 0)
                {
                    skip -= width;
                    in += iter.character_length();
                }
            }
        }
    }

    const char* end = in;
    const char* end_truncate = nullptr;
    int32 cells_truncate = 0;
    bool limited = false;
    const int32 reserve = out ? ellipsis_cells : 0;
    while (true)
    {
        end = iter.get_pointer();
        const int32 c = iter.next();
        if (!c)
            break;
        const int32 width = iter.character_wcwidth_onectrl();
        if (cells + width > limit - reserve)
        {
            if (!end_truncate)
            {
                end_truncate = iter.character_pointer();
                cells_truncate = cells;
            }
            if (cells + width > limit)
            {
                end = end_truncate;
                cells = cells_truncate;
                limited = true;
                break;
            }
        }
        cells += width;
    }

    if (out && (out->length() || limited))
    {
        out->concat(in, int32(end - in));
        if (limited && cells + ellipsis_cells <= limit)
        {
            out->concat(ellipsis, ellipsis_len);
            cells += ellipsis_cells;
        }
        if (text_ptr)
            *text_ptr = out->c_str();
        return out->length();
    }

    if (text_ptr)
        *text_ptr = in;
    return int32(end - in);
}

//------------------------------------------------------------------------------
static bool strstr_compare(const str_base& needle, const char* haystack)
{
    if (haystack && *haystack)
    {
        str_iter sift(haystack);
        while (sift.more())
        {
            int32 cmp = str_compare(needle.c_str(), sift.get_pointer());
            if (cmp == -1 || cmp == needle.length())
                return true;
            sift.next();
        }
    }

    return false;
}



//------------------------------------------------------------------------------
static void standalone_textlist_sighandler(int32 sig)
{
    // raise() clears the signal handler, so set it again.
    signal(sig, standalone_textlist_sighandler);
    clink_set_signaled(sig);
}



//------------------------------------------------------------------------------
popup_results::popup_results(popup_result result, int32 index, const char* text)
    : m_result(result)
    , m_index(index)
    , m_text(text)
{
}

//------------------------------------------------------------------------------
void popup_results::clear()
{
    m_result = popup_result::cancel;
    m_index = -1;
    m_text.free();
}



//------------------------------------------------------------------------------
textlist_impl::addl_columns::addl_columns(textlist_impl::item_store& store)
    : m_store(store)
{
}

//------------------------------------------------------------------------------
const char* textlist_impl::addl_columns::get_col_text(int32 row, int32 col) const
{
    return m_rows[row].column[col];
}

//------------------------------------------------------------------------------
int32 textlist_impl::addl_columns::get_col_longest(int32 col) const
{
    return m_longest[col];
}

//------------------------------------------------------------------------------
int32 textlist_impl::addl_columns::get_col_layout_width(int32 col) const
{
    return m_layout_width[col];
}

//------------------------------------------------------------------------------
const char* textlist_impl::addl_columns::add_entry(const char* ptr)
{
    size_t len_match = strlen(ptr);
    ptr += len_match + 1;

    const char* display = ptr;
    size_t len_display = strlen(ptr);
    ptr += len_display + 1;

    add_columns(ptr);

    return display;
}

//------------------------------------------------------------------------------
void textlist_impl::addl_columns::add_columns(const char* ptr)
{
    column_text column_text = {};
    if (*ptr)
    {
        str<> tmp;
        int32 col = 0;
        bool any_tabs = false;
        while (col < sizeof_array(column_text.column))
        {
            const char* tab = strchr(ptr, '\t');
            const int32 cells = make_column(ptr, tab, tmp);
            column_text.column[col] = m_store.add(tmp.c_str());
            m_longest[col] = max<int32>(m_longest[col], cells);
            ptr = tab;
            if (!ptr)
                break;
            any_tabs = true;
            col++;
            ptr++;
        }
        m_any_tabs |= any_tabs;
    }

    m_rows.emplace_back(std::move(column_text));
}

//------------------------------------------------------------------------------
void textlist_impl::addl_columns::erase_row(int32 row)
{
    assert(row >= 0 && row < m_rows.size());
    if (row >= 0 && row < m_rows.size())
        m_rows.erase(m_rows.begin() + row);
}

//------------------------------------------------------------------------------
int32 textlist_impl::addl_columns::calc_widths(int32 available)
{
    memset(&m_layout_width, 0, sizeof(m_layout_width));

    bool pending[_countof(m_layout_width)] = {};
    for (int32 col = _countof(m_layout_width); col--;)
    {
        if (m_longest[col])
        {
            pending[col] = true;
            available -= col_padding;
        }
    }

    if (available > 0)
    {
        int32 divisor = 0;
        for (int32 col = _countof(m_layout_width); col--;)
        {
            if (pending[col])
                ++divisor;
        }

        while (divisor)
        {
            bool share = true;

            const int32 threshold = available / divisor;
            for (int32 col = _countof(m_layout_width); col--;)
            {
                if (pending[col] && m_longest[col] <= threshold)
                {
                    m_layout_width[col] = m_longest[col];
                    available -= m_longest[col];
                    --divisor;
                    pending[col] = false;
                    share = false;
                }
            }

            if (share)
            {
                if (divisor)
                {
                    for (int32 col = _countof(m_layout_width); col--;)
                    {
                        if (pending[col])
                        {
                            m_layout_width[col] = available / divisor;
                            available -= threshold;
                        }
                    }
                }
                break;
            }
        }
    }

    return available;
}

//------------------------------------------------------------------------------
bool textlist_impl::addl_columns::get_any_tabs() const
{
    return m_any_tabs;
}

//------------------------------------------------------------------------------
void textlist_impl::addl_columns::clear()
{
    std::vector<column_text> zap;
    m_rows = std::move(zap);
    memset(&m_longest, 0, sizeof(m_longest));
    memset(&m_layout_width, 0, sizeof(m_longest));
    m_any_tabs = false;
}



//------------------------------------------------------------------------------
textlist_impl::textlist_impl(input_dispatcher& dispatcher)
    : m_dispatcher(dispatcher)
    , m_columns(m_store)
{
}

//------------------------------------------------------------------------------
popup_results textlist_impl::activate(const char* title, const char** entries, int32 count, int32 index, bool reverse, textlist_mode mode, entry_info* infos, bool has_columns, const popup_config* config)
{
    if (s_old_default_popup_search_mode != g_popup_search_mode.get())
    {
        s_old_default_popup_search_mode = g_popup_search_mode.get();
        s_default_popup_search_mode = s_old_default_popup_search_mode;
    }

    reset();
    m_results.clear();

    if (!s_standalone)
    {
        assert(m_buffer);
        if (!m_buffer)
            return popup_result::error;

        // Doesn't make sense to record macro with a popup list.
        if (RL_ISSTATE(RL_STATE_MACRODEF) != 0)
            return popup_result::error;
    }

    if (!entries || count <= 0)
        return popup_result::error;

    // Attach to list of items.
    m_entries = entries;
    m_infos = infos;
    m_count = count;
    m_original_count = count;

    // Initialize the various modes.
    m_reverse = config ? config->reverse : reverse;
    m_pref_height = config ? config->height : 0;
    m_pref_width = config ? config->width : 0;
    m_mode = mode;
    m_history_mode = is_history_mode(mode);
    m_was_default_search_mode = (!config || config->search_mode < 0);
    m_filter = (m_was_default_search_mode ? s_default_popup_search_mode : config->search_mode) > 0;
    m_show_numbers = m_history_mode;
    m_win_history = (mode == textlist_mode::win_history);
    m_del_callback = config ? config->del_callback : nullptr;

    // Make sure there's room.
    update_layout();
    if (m_visible_rows <= 0)
    {
        reset();
        return popup_result::error;
    }

    // Signal handler when standalone, so Ctrl-Break can erase the popup list.
    typedef void (__cdecl sig_func_t)(int32);
    sig_func_t* old_int = nullptr;
    sig_func_t* old_break = nullptr;
    if (s_standalone)
    {
        old_int = signal(SIGINT, standalone_textlist_sighandler);
        old_break = signal(SIGBREAK, standalone_textlist_sighandler);
        clink_install_ctrlevent();
    }

    // Initialize colors.
    init_colors(config);

    // Maybe format history timestamps.
    const bool history_timestamps = (m_history_mode &&
        ((g_history_timestamp.get() == 2 && (!rl_explicit_arg || rl_numeric_arg)) ||
         (g_history_timestamp.get() == 1 && rl_explicit_arg && rl_numeric_arg)));
    const HIST_ENTRY* const* const histlist = history_list();
    assertimplies(history_timestamps, !has_columns);
    history_timeformatter timeformatter;
    if (history_timestamps)
        timeformatter.set_timeformat(nullptr, true);

#ifdef USE_MEMORY_TRACKING
    sane_alloc_config sane = dbggetsaneallocconfig();
    if (history_timestamps)
        dbgsetsanealloc(max<int32>(count * (32 + (sizeof(void*)*3)), 256*1024), 1024*1024, nullptr);
#endif

    // Gather the items.
    str<> tmp;
    str<> tmp2;
    for (int32 i = 0; i < count; i++)
    {
        const char* text;
        if (has_columns)
            text = m_columns.add_entry(m_entries[i]);
        else
        {
            text = m_entries[i];
            if (history_timestamps)
            {
                const int32 j = m_infos ? m_infos[i].index : i;
                const char* timestamp = histlist[j]->timestamp;
                tmp2.clear();
                if (timestamp && *timestamp)
                {
                    const time_t tt = time_t(atoi(timestamp));
                    timeformatter.format(tt, tmp);
                    tmp2.format("%-*s\t", timeformatter.max_timelen(), tmp.c_str());
                }
                m_columns.add_columns(tmp2.c_str());
            }
        }
        m_longest = max<int32>(m_longest, make_item(text, tmp));
        m_items.push_back(m_store.add(tmp.c_str()));
    }
    m_has_columns = has_columns || history_timestamps;

    if (title && *title)
        m_default_title = title;

    // Initialize the view.
    if (index < 0 || index >= m_count)
    {
        m_index = m_count - 1;
        m_top = max<int32>(0, m_count - m_visible_rows);
    }
    else
    {
        m_index = index;
        m_top = max<int32>(0, min<int32>(m_index - (m_visible_rows / 2), m_count - m_visible_rows));
    }

    show_cursor(false);
    lock_cursor(true);

    assert(!m_active);
    m_active = true;
    m_reset_history_index = false;
    update_display();

    m_dispatcher.dispatch(m_bind_group);

    // Cancel if the dispatch loop is left unexpectedly (e.g. certain errors).
    if (m_active)
        cancel(popup_result::cancel);

    assert(!m_active);
    update_display();

    if (!s_standalone && !clink_is_signaled())
    {
        _rl_refresh_line();
        rl_display_fixed = 1;
    }

    lock_cursor(false);
    show_cursor(true);

    popup_results results;
    results.m_result = m_results.m_result;
    results.m_index = m_results.m_index;
    results.m_text = std::move(m_results.m_text);

    reset();
    m_results.clear();

    if (s_standalone)
    {
        clink_shutdown_ctrlevent();
        signal(SIGBREAK, old_break);
        signal(SIGINT, old_int);
        // Re-raise the signal so the interpreter's signal handler can respond.
        const int32 sig = clink_is_signaled();
        if (sig)
            raise(sig);
    }

#ifdef USE_MEMORY_TRACKING
    if (history_timestamps)
        dbgsetsaneallocconfig(sane);
#endif

    return results;
}

//------------------------------------------------------------------------------
bool textlist_impl::is_active() const
{
    return m_active;
}

//------------------------------------------------------------------------------
bool textlist_impl::accepts_mouse_input(mouse_input_type type) const
{
    switch (type)
    {
    case mouse_input_type::left_click:
    case mouse_input_type::double_click:
    case mouse_input_type::wheel:
    case mouse_input_type::drag:
        return true;
    default:
        return false;
    }
}

//------------------------------------------------------------------------------
void textlist_impl::bind_input(binder& binder)
{
    const char* esc = get_bindable_esc();

    m_bind_group = binder.create_group("textlist");

    binder.bind(m_bind_group, "\\e[A", bind_id_textlist_up);            // Up
    binder.bind(m_bind_group, "\\e[B", bind_id_textlist_down);          // Down
    binder.bind(m_bind_group, "\\e[D", bind_id_textlist_left);          // Left
    binder.bind(m_bind_group, "\\e[C", bind_id_textlist_right);         // Right
    binder.bind(m_bind_group, "\\e[1;5D", bind_id_textlist_ctrlleft);   // Ctrl+Left
    binder.bind(m_bind_group, "\\e[1;5C", bind_id_textlist_ctrlright);  // Ctrl+Right
    binder.bind(m_bind_group, "\\e[5~", bind_id_textlist_pgup);         // PgUp
    binder.bind(m_bind_group, "\\e[6~", bind_id_textlist_pgdn);         // PgDn
    binder.bind(m_bind_group, "\\e[H", bind_id_textlist_home);          // Home
    binder.bind(m_bind_group, "\\e[F", bind_id_textlist_end);           // End
    binder.bind(m_bind_group, "\\e[1;5H", bind_id_textlist_ctrlhome);   // Ctrl+Home
    binder.bind(m_bind_group, "\\e[1;5F", bind_id_textlist_ctrlend);    // Ctrl+End
    binder.bind(m_bind_group, "\\eOR", bind_id_textlist_findnext);      // F3
    binder.bind(m_bind_group, "\\e[1;2R", bind_id_textlist_findprev);   // Shift+F3
    binder.bind(m_bind_group, "^l", bind_id_textlist_findnext);         // Ctrl+L
    binder.bind(m_bind_group, "\\e[27;6;76~", bind_id_textlist_findprev); // Ctrl+Shift+L
    binder.bind(m_bind_group, "^c", bind_id_textlist_copy);             // Ctrl+C
    binder.bind(m_bind_group, "^h", bind_id_textlist_backspace);        // Backspace
    binder.bind(m_bind_group, "\\r", bind_id_textlist_enter);           // Enter
    binder.bind(m_bind_group, "\\e[27;2;13~", bind_id_textlist_insert); // Shift+Enter
    binder.bind(m_bind_group, "\\e[27;5;13~", bind_id_textlist_insert); // Ctrl+Enter

    binder.bind(m_bind_group, "^d", bind_id_textlist_delete);           // Ctrl+D
    binder.bind(m_bind_group, "\\e[3~", bind_id_textlist_delete);       // Del

    binder.bind(m_bind_group, "^g", bind_id_textlist_escape);           // Ctrl+G
    if (esc)
        binder.bind(m_bind_group, esc, bind_id_textlist_escape);        // Esc

    binder.bind(m_bind_group, "\\e[$*;*L", bind_id_textlist_leftclick, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*;*D", bind_id_textlist_doubleclick, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*A", bind_id_textlist_wheelup, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*B", bind_id_textlist_wheeldown, true/*has_params*/);
    binder.bind(m_bind_group, "\\e[$*;*M", bind_id_textlist_drag, true/*has_params*/);

    binder.bind(m_bind_group, "\\eOS", bind_id_textlist_togglefilter);  // F4
    binder.bind(m_bind_group, "\\eOP", bind_id_textlist_help);          // F1

    binder.bind(m_bind_group, "", bind_id_textlist_catchall);
}

//------------------------------------------------------------------------------
void textlist_impl::on_begin_line(const context& context)
{
    assert(!s_textlist);
    s_textlist = this;
    m_buffer = &context.buffer;
    m_printer = &context.printer;

    m_scroll_helper.clear();

    m_screen_cols = context.printer.get_columns();
    m_screen_rows = context.printer.get_rows();
    update_layout();
}

//------------------------------------------------------------------------------
void textlist_impl::on_end_line()
{
    s_textlist = nullptr;
    m_buffer = nullptr;
    m_printer = nullptr;
}

//------------------------------------------------------------------------------
static void advance_index(int32& i, int32 direction, int32 max_count)
{
    i += direction;
    if (direction < 0)
    {
        if (i < 0)
            i = max_count - 1;
    }
    else
    {
        if (i >= max_count)
            i = 0;
    }
}

//------------------------------------------------------------------------------
void textlist_impl::on_input(const input& input, result& result, const context& /*context*/)
{
    assert(m_active);

    bool set_input_clears_needle = true;
    bool from_begin = false;
    bool need_display = false;
    bool advance_before_find = false;

    // Cancel if no room.
    if (m_visible_rows <= 0)
    {
        cancel(popup_result::cancel);
        return;
    }

    switch (input.id)
    {
    case bind_id_textlist_up:
        m_index--;
        if (m_index < 0)
            m_index = _rl_menu_complete_wraparound ? m_count - 1 : 0;
navigated:
        update_display();
        break;
    case bind_id_textlist_down:
        m_index++;
        if (m_index >= m_count)
            m_index = _rl_menu_complete_wraparound ? 0 : m_count - 1;
        goto navigated;

    case bind_id_textlist_home:
        m_index = 0;
        goto navigated;
    case bind_id_textlist_end:
        m_index = m_count - 1;
        goto navigated;

    case bind_id_textlist_pgup:
    case bind_id_textlist_pgdn:
        {
            const int32 y = m_index;
            const int32 rows = min<int32>(m_count, m_visible_rows);

            // Use rows as the page size (vs the more common rows-1) for
            // compatibility with Conhost's F7 popup list behavior.
            if (input.id == bind_id_textlist_pgup)
            {
                if (y > 0)
                {
                    int32 new_y = max<int32>(0, (y == m_top) ? y - rows : m_top);
                    m_index += (new_y - y);
                    goto navigated;
                }
            }
            else if (input.id == bind_id_textlist_pgdn)
            {
                if (y < m_count - 1)
                {
                    int32 bottom_y = m_top + rows - 1;
                    int32 new_y = min<int32>(m_count - 1, (y == bottom_y) ? y + rows : bottom_y);
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

    case bind_id_textlist_findnext:
    case bind_id_textlist_findprev:
        set_input_clears_needle = false;
        if (m_win_history)
            break;
        advance_before_find = true;
find:
        if (m_win_history)
        {
            assert(!s_standalone);
            lock_cursor(false);
            show_cursor(true);
            rl_ding();
            show_cursor(false);
            lock_cursor(true);
        }
        else if (m_filter)
        {
            if (filter_items())
            {
                m_prev_displayed = -1;
                m_force_clear = true;
                update_display();
            }
        }
        else
        {
            int32 direction = (input.id == bind_id_textlist_findprev) ? -1 : 1;
            if (m_reverse)
                direction = 0 - direction;

            int32 mode = g_ignore_case.get();
            if (mode < 0 || mode >= str_compare_scope::num_scope_values)
                mode = str_compare_scope::exact;
            str_compare_scope _(mode, g_fuzzy_accent.get());

            int32 i = m_index;
            if (from_begin)
                i = m_reverse ? m_count - 1 : 0;

            if (advance_before_find)
                advance_index(i, direction, m_count);

            int32 original = i;
            while (true)
            {
                bool match = strstr_compare(m_needle, get_item_text(i));
                if (m_has_columns)
                {
                    for (int32 col = 0; !match && col < max_columns; col++)
                        match = strstr_compare(m_needle, m_columns.get_col_text(i, col));
                }

                if (match)
                {
                    m_index = i;
                    if (m_index < m_top || m_index >= m_top + m_visible_rows)
                        m_top = max<int32>(0, min<int32>(m_index - (m_visible_rows / 2), m_count - m_visible_rows));
                    m_prev_displayed = -1;
                    need_display = true;
                    break;
                }

                advance_index(i, direction, m_count);
                if (i == original)
                    break;
            }

            if (need_display)
                update_display();
        }
        break;

    case bind_id_textlist_copy:
        {
            const char* text = m_entries[m_index];
            os::set_clipboard_text(text, int32(strlen(text)));
            set_input_clears_needle = false;
        }
        break;

    case bind_id_textlist_delete:
        {
            // Remove the entry.
            const int32 original_index = get_original_index(m_index);
            const int32 external_index = m_infos ? m_infos[original_index].index : original_index;
            if (m_history_mode)
            {
                m_reset_history_index = true;
                // Remove the corresponding persisted history entry.
                host_remove_history(external_index, nullptr);
                // Remove the corresponding entry from Readline's copy of history.
                HIST_ENTRY* hist = remove_history(external_index);
                free_history_entry(hist);
            }
            else if (m_mode == textlist_mode::directories)
            {
                if (!host_remove_dir_history(external_index))
                {
                    rl_ding();
                    break;
                }
            }
            else if (m_del_callback)
            {
                if (!m_del_callback(external_index))
                {
                    rl_ding();
                    break;
                }
            }
            else
            {
                break;
            }

            // Remove the item from the popup list.
            const int32 old_rows = min<int32>(m_visible_rows, m_count);
            int32 move_count = (m_original_count - 1) - original_index;
            memmove(m_entries + original_index, m_entries + original_index + 1, move_count * sizeof(m_entries[0]));
            m_items.erase(m_items.begin() + original_index);
            if (m_has_columns)
                m_columns.erase_row(original_index);
            if (m_infos)
            {
                memmove(m_infos + original_index, m_infos + original_index + 1, move_count * sizeof(m_infos[0]));
                for (int32 i = m_original_count - 1; i-- > original_index;)
                    m_infos[i].index--;
            }
            if (!m_filtered_items.empty())
            {
                m_filtered_items.erase(m_filtered_items.begin() + m_index);
                for (int32 i = m_count - 1; i-- > m_index;)
                    m_filtered_items[i]--;
            }
            m_count--;
            m_original_count--;
            if (!m_original_count)
            {
                cancel(popup_result::cancel);
                return;
            }

            // Move index.
            if (m_index > 0)
                m_index--;

            // Redisplay.
            {
                const int32 new_rows = min<int32>(m_visible_rows, m_count);
                if (new_rows < old_rows)
                    m_force_clear = true;

                update_layout();

                int32 delta = m_index - m_top;
                if (delta >= m_visible_rows - 1)
                    delta = m_visible_rows - 2;
                if (delta <= 0)
                    delta = 1;
                if (delta >= m_visible_rows)
                    delta = 0;

                int32 top = max<int32>(0, m_index - delta);
                const int32 max_top = max<int32>(0, m_count - m_visible_rows);
                if (top > max_top)
                    top = max_top;
                set_top(top);

                m_prev_displayed = -1;
                update_display();
            }
        }
        break;

    case bind_id_textlist_escape:
        cancel(popup_result::cancel);
        return;

    case bind_id_textlist_enter:
        if (m_index < 0 || m_index >= m_count)
            break;
        cancel(popup_result::use);
        return;

    case bind_id_textlist_insert:
do_insert:
        if (m_index < 0 || m_index >= m_count)
            break;
        cancel(popup_result::select);
        return;

    case bind_id_textlist_leftclick:
    case bind_id_textlist_doubleclick:
    case bind_id_textlist_drag:
        {
            const uint32 now = m_scroll_helper.on_input();

            uint32 p0, p1;
            input.params.get(0, p0);
            input.params.get(1, p1);
            const uint32 rows = min<int32>(m_count, m_visible_rows);

#ifdef SHOW_VERT_SCROLLBARS
            if (m_vert_scroll_car &&
                (input.id == bind_id_textlist_leftclick ||
                 input.id == bind_id_textlist_doubleclick))
            {
                const int32 row = p1 - m_mouse_offset;
                m_scroll_bar_clicked = (p0 == m_vert_scroll_column && row >= 0 && row < rows);
            }

            if (m_scroll_bar_clicked)
            {
                const int32 row = min<int32>(max<int32>(p1 - m_mouse_offset, 0), rows - 1);
                m_index = hittest_scroll_car(row, rows, m_count);
                set_top(min<int32>(max<int32>(m_index - ((rows - 1) / 2), 0), m_count - rows));
                goto navigated;
            }
#endif

            if (input.id != bind_id_textlist_drag)
            {
                if (int32(p1) < m_mouse_offset - 1 || p1 >= m_mouse_offset - 1 + rows + 2/*border*/)
                {
                    cancel(popup_result::cancel);
                    return;
                }
                if (p0 < m_mouse_left || p0 >= m_mouse_left + m_mouse_width)
                    break;
            }
            p1 -= m_mouse_offset;
            if (p1 < rows)
            {
                m_index = p1 + m_top;
                if (input.id == bind_id_textlist_doubleclick)
                {
                    update_display();
                    cancel(popup_result::use);
                    return;
                }
                goto navigated;
            }
            else if (input.id == bind_id_textlist_drag && m_scroll_helper.can_scroll())
            {
                if (int32(p1) < 0)
                {
                    if (m_top > 0)
                    {
                        set_top(max<int32>(0, m_top - m_scroll_helper.scroll_speed()));
                        m_index = m_top;
                        goto navigated;
                    }
                }
                else
                {
                    if (m_top + rows < m_count)
                    {
                        set_top(min<int32>(m_count - rows, m_top + m_scroll_helper.scroll_speed()));
                        m_index = m_top + rows - 1;
                        goto navigated;
                    }
                }
            }
        }
        break;

    case bind_id_textlist_wheelup:
    case bind_id_textlist_wheeldown:
        {
            uint32 p0;
            input.params.get(0, p0);
            if (input.id == bind_id_textlist_wheelup)
                m_index -= min<uint32>(m_index, p0);
            else
                m_index += min<uint32>(m_count - 1 - m_index, p0);
            goto navigated;
        }
        break;

    case bind_id_textlist_left:
        if (m_win_history)
            goto do_insert;
        adjust_horz_offset(-1);
        break;
    case bind_id_textlist_right:
        if (m_win_history)
            goto do_insert;
        adjust_horz_offset(+1);
        break;
    case bind_id_textlist_ctrlleft:
        adjust_horz_offset(-16);
        break;
    case bind_id_textlist_ctrlright:
        adjust_horz_offset(+16);
        break;
    case bind_id_textlist_ctrlhome:
        adjust_horz_offset(-999999);
        break;
    case bind_id_textlist_ctrlend:
        adjust_horz_offset(+999999);
        break;

    case bind_id_textlist_togglefilter:
        if (!m_win_history)
        {
            if (m_filter)
                clear_filter();
            m_filter = !m_filter;
            if (m_was_default_search_mode)
                s_default_popup_search_mode = m_filter ? 1 : 0;
            need_display = true;
            goto update_needle;
        }
        break;
    case bind_id_textlist_help:
        AllowSetForegroundWindow(ASFW_ANY);
        ShellExecute(0, nullptr, "https://chrisant996.github.io/clink/clink.html#popup-windows", 0, 0, SW_NORMAL);
        break;

    case bind_id_textlist_backspace:
    case bind_id_textlist_catchall:
        {
            set_input_clears_needle = false;

            if (input.id == bind_id_textlist_backspace)
            {
                if (!m_needle.length())
                    break;
                int32 point = _rl_find_prev_mbchar(const_cast<char*>(m_needle.c_str()), m_needle.length(), MB_FIND_NONZERO);
                m_needle.truncate(point);
                need_display = true;
                from_begin = !m_win_history;
                goto update_needle;
            }

            // Collect the input.
            {
                if (m_input_clears_needle)
                {
                    assert(!m_win_history);
                    if (!m_filter)
                    {
                        m_needle.clear();
                        m_needle_is_number = false;
                    }
                    m_input_clears_needle = false;
                }

                str_iter iter(input.keys, input.len);
                const char* seq = iter.get_pointer();
                while (iter.more())
                {
                    uint32 c = iter.next();
                    if (!m_win_history)
                    {
                        m_override_title.clear();
                        m_needle.concat(seq, int32(iter.get_pointer() - seq));
                        need_display = true;
                    }
                    else if (c >= '0' && c <= '9')
                    {
                        if (!m_needle_is_number)
                        {
                            need_display = need_display || m_has_override_title;
                            m_override_title.clear();
                            m_needle.clear();
                            m_needle_is_number = true;
                        }
                        if (m_needle.length() < 6)
                        {
                            char digit = char(c);
                            m_needle.concat(&digit, 1);
                        }
                    }
                    else
                    {
                        need_display = need_display || m_has_override_title;
                        m_override_title.clear();
                        m_needle.clear();
                        m_needle.concat(seq, int32(iter.get_pointer() - seq));
                        m_needle_is_number = false;
                    }
                    seq = iter.get_pointer();
                }
            }

            // Handle the input.
update_needle:
            if (!m_win_history)
            {
                advance_before_find = false;
                m_override_title.clear();
                if (m_needle.length())
                    m_override_title.format("%s: %-10s", m_filter ? "filter" : "find", m_needle.c_str());
                goto find;
            }
            else if (m_needle_is_number)
            {
                if (m_needle.length())
                {
                    need_display = true;
                    m_override_title.clear();
                    m_override_title.format("enter history number: %-6s", m_needle.c_str());
                    int32 i = atoi(m_needle.c_str());
                    if (m_infos)
                    {
                        int32 lookup = 0;
                        char lookupstr[16];
                        char needlestr[16];
                        _itoa_s(i, needlestr, 10);
                        const int32 needlestr_len = int32(strlen(needlestr));
                        while (lookup < m_count)
                        {
                            _itoa_s(get_item_info(lookup).index + 1, lookupstr, 10);
                            if (strncmp(needlestr, lookupstr, needlestr_len) == 0)
                            {
                                i = lookup;
                                break;
                            }
                            lookup++;
                        }
                        // If the input history history number isn't found, i is
                        // m_count and correctly skips updating m_index.
                        i = lookup;
                    }
                    else
                    {
                        i--;
                    }
                    if (i >= 0 && i < m_count)
                    {
                        m_index = i;
                        if (m_index < m_top || m_index >= m_top + m_visible_rows)
                            m_top = max<int32>(0, min<int32>(m_index - (m_visible_rows / 2), m_count - m_visible_rows));
                        m_prev_displayed = -1;
                        need_display = true;
                    }
                }
                else if (m_override_title.length())
                {
                    need_display = true;
                    m_override_title.clear();
                }
            }
            else if (m_needle.length())
            {
                advance_before_find = true;
                goto find;
            }

            if (need_display)
                update_display();
        }
        break;
    }

    if (set_input_clears_needle && !m_win_history)
        m_input_clears_needle = true;

    // Keep dispatching input.
    result.loop();
}

//------------------------------------------------------------------------------
void textlist_impl::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
}

//------------------------------------------------------------------------------
void textlist_impl::on_terminal_resize(int32 columns, int32 rows, const context& context)
{
    m_screen_cols = columns;
    m_screen_rows = rows;

    if (m_active)
        cancel(popup_result::cancel);
}

//------------------------------------------------------------------------------
void textlist_impl::on_signal(int32 sig)
{
    if (m_active)
    {
        rollback<volatile int32> rb_sig(_rl_caught_signal, 0);
        m_active = false;
        update_display();
        if (!s_standalone)
        {
            force_signaled_redisplay();
            _rl_refresh_line();
            rl_display_fixed = 1;
        }
        m_active = true;
    }
}

//------------------------------------------------------------------------------
void textlist_impl::cancel(popup_result result)
{
    assert(m_active);

    m_results.clear();
    m_results.m_result = result;
    if (result == popup_result::use || result == popup_result::select)
    {
        if (m_index >= 0 && m_index < m_count)
        {
            const int32 original_index = get_original_index(m_index);
            m_results.m_index = original_index;
            m_results.m_text = m_entries[original_index];
        }
    }

    if (!s_standalone && m_reset_history_index)
    {
        rl_replace_line("", 1);
        using_history();
        m_reset_history_index = false;
    }

    m_active = false;
}

//------------------------------------------------------------------------------
void textlist_impl::update_layout()
{
    int32 slop_rows = 2;
    int32 border_rows = 2;
    int32 target_rows = m_pref_height;

    if (target_rows)
    {
        m_visible_rows = min<int32>(target_rows, m_screen_rows - border_rows - slop_rows);
    }
    else
    {
        target_rows = m_history_mode ? 20 : 10;
        m_visible_rows = min<int32>(target_rows, (m_screen_rows / 2) - border_rows - slop_rows);
    }

    if (m_screen_cols <= min_screen_cols)
        m_visible_rows = 0;

    m_max_num_cells = 0;
    if (m_show_numbers && m_original_count > 0)
    {
        str<> tmp;
        tmp.format("%u: ", m_infos ? m_infos[m_original_count - 1].index + 1 : m_original_count);
        m_max_num_cells = tmp.length();
    }

#ifdef SHOW_VERT_SCROLLBARS
    m_vert_scroll_car = calc_scroll_car_size(m_visible_rows, m_count);
    m_vert_scroll_column = 0;
#endif
}

//------------------------------------------------------------------------------
void textlist_impl::update_top()
{
    const int32 y = m_index;
    if (m_top > y)
    {
        set_top(y);
    }
    else
    {
        const int32 rows = min<int32>(m_count, m_visible_rows);
        int32 top = max<int32>(0, y - max<int32>(rows - 1, 0));
        if (m_top < top)
            set_top(top);
    }
    assert(m_top >= 0);
    assert(m_top <= max<int32>(0, m_count - m_visible_rows));
}

//------------------------------------------------------------------------------
static void make_horz_border(const char* message, int32 col_width, bool bars, str_base& out,
                             const char* header_color=nullptr, const char* border_color=nullptr)
{
    out.clear();

    if (!message || !*message)
    {
        while (col_width-- > 0)
            out.concat("\xe2\x94\x80", 3);
        return;
    }

    if (!header_color || !border_color || _strcmpi(header_color, border_color) == 0)
    {
        header_color = nullptr;
        border_color = nullptr;
    }

    int32 cells = 0;
    int32 len = 0;

    {
        const char* walk = message;
        int32 remaining = col_width - (2 + 2); // Bars, spaces.
        wcwidth_iter iter(message);
        while (iter.next())
        {
            const int32 width = iter.character_wcwidth_onectrl();
            if (width > remaining)
                break;
            cells += width;
            remaining -= width;
            len += iter.character_length();
        }
    }

    int32 x = (col_width - cells) / 2;
    x--;

    for (int32 i = x; i-- > 0;)
    {
        if (i == 0 && bars)
            out.concat("\xe2\x94\xa4", 3);
        else
            out.concat("\xe2\x94\x80", 3);
    }

    x += 1 + cells + 1;
    if (header_color && border_color)
        out.concat(header_color);
    out.concat(" ", 1);
    out.concat(message, len);
    out.concat(" ", 1);
    if (header_color && border_color)
        out.concat(border_color);

    bool cap = bars;
    for (int32 i = col_width - x; i-- > 0;)
    {
        if (cap)
        {
            cap = false;
            out.concat("\xe2\x94\x9c", 3);
        }
        else
        {
            out.concat("\xe2\x94\x80", 3);
        }
    }
}

//------------------------------------------------------------------------------
void textlist_impl::update_display()
{
    const bool is_filter_active = (m_original_count && !m_filter_string.empty());
    if (m_visible_rows > 0 || is_filter_active)
    {
        // Remember the cursor position so it can be restored later to stay
        // consistent with Readline's view of the world.
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(h, &csbi);
        COORD restore = csbi.dwCursorPosition;
        const int32 vpos = _rl_last_v_pos;
        const int32 cpos = _rl_last_c_pos;
        int32 up = 0;

        // Move cursor to next line.  I.e. the list goes immediately below the
        // cursor line and may overlay some lines of input.
        if (!s_standalone || restore.X > 0)
        {
            m_printer->print("\n");
            up++;
        }

        // Display list.
        bool move_to_end = true;
        const int32 count = m_count;
        str<> line;
        if (m_active && (count > 0 || is_filter_active))
        {
            update_top();

            const bool draw_border = (m_prev_displayed < 0) || m_override_title.length() || m_has_override_title;
            m_has_override_title = !m_override_title.empty();

            const bool show_del = (m_history_mode || m_mode == textlist_mode::directories || m_del_callback);
            str<> footer;
            if (show_del)
                footer.concat("Del=Delete");
            if (m_history_mode)
            {
                if (!footer.empty())
                    footer.concat("  ");
                footer.concat("Enter=Execute");
            }
            if (!m_needle.empty())
            {
                if (!footer.empty())
                    footer.concat("  ");
                footer.concat("F4=Mode");
            }

            int32 longest;
            if (m_pref_width)
            {
                longest = m_pref_width;
                longest = max<int32>(longest, 10); // Too narrow doesn't draw well.
            }
            else
            {
                longest = m_longest + m_max_num_cells;
                if (m_has_columns)
                {
                    for (int32 i = 0; i < max_columns; i++)
                    {
                        const int32 x = m_columns.get_col_longest(i);
                        if (x)
                            longest += 2 + x;
                    }
                }
                longest = max<int32>(longest, max<int32>(40, cell_count(footer.c_str())));
            }

            longest = max<int32>(longest, cell_count(m_default_title.c_str()) + 4);

            const int32 effective_screen_cols = (m_screen_cols < 40) ? m_screen_cols : max<int32>(40, m_screen_cols - 4);
            const int32 popup_width = min<int32>(longest + 2, effective_screen_cols); // +2 for borders.
            const int32 content_width = popup_width - 2;

            const bool clear_eol = (content_width < m_prev_content_width);
            if (content_width != m_prev_content_width)
                m_prev_displayed = -1;
            m_prev_content_width = content_width;

            str<> noescape;
            str<> left;
            str<> horzline;
            str<> tmp;

            {
                int32 x = csbi.dwCursorPosition.X - ((popup_width + 1) / 2);
                int32 center_x = (m_screen_cols - effective_screen_cols) / 2;
                if (x + popup_width > center_x + effective_screen_cols)
                    x = m_screen_cols - center_x - popup_width;
                if (x < center_x)
                    x = center_x;
                if (x > 0)
                    left.format("\x1b[%uG", x + 1);
                m_mouse_left = x + 1;
                m_mouse_width = content_width;
            }

            if (m_prev_displayed < 0)
            {
                m_horz_scroll_range = 0;
                m_horz_item_enabled = 0;
                memset(m_horz_column_enabled, 0, sizeof(m_horz_column_enabled));
                m_item_cells = max<int32>(0, min<int32>(m_longest, (m_mouse_width / (m_has_columns ? 2 : 1)) - m_max_num_cells));

                if (m_item_cells > 0)
                {
                    // Add columns to total width.
                    if (m_has_columns)
                        m_item_cells += m_columns.calc_widths(m_mouse_width - (m_max_num_cells + m_item_cells));

                    // Identify which columns need horizontal scrolling.
                    m_horz_item_enabled = (m_item_cells < m_longest);
                    if (m_has_columns)
                    {
                        for (int32 col = max_columns; col--;)
                            m_horz_column_enabled[col] = (m_columns.get_col_layout_width(col) < m_columns.get_col_longest(col));
                    }

                    // Init the range for horizontal scrolling.
                    uint32 longest_visible = 0;
                    const int32 rows = min<int32>(m_count, m_visible_rows);
                    for (int32 row = 0; row < rows; ++row)
                    {
                        // Expand control characters to "^A" etc.
                        longest_visible = max<int32>(longest_visible, make_item(get_item_text(m_top + row), tmp));
                    }
                    m_horz_scroll_range = max<int32>(m_horz_scroll_range, longest_visible - m_item_cells);
                    if (m_has_columns)
                    {
                        for (int32 col = max_columns; col--;)
                        {
                            longest_visible = 0;
                            for (int32 row = 0; row < rows; ++row)
                            {
                                const char* col_text = get_col_text(m_top + row, col);
                                if (col_text)
                                {
                                    // Expand control characters to "^A" etc.
                                    longest_visible = max<int32>(longest_visible, make_item(col_text, tmp));
                                }
                            }
                            m_horz_scroll_range = max<int32>(m_horz_scroll_range, longest_visible - m_columns.get_col_layout_width(col));
                        }
                    }
                }
            }

            // Display border.
            if (draw_border)
            {
                make_horz_border(m_has_override_title ? m_override_title.c_str() : m_default_title.c_str(), content_width, m_has_override_title, horzline,
                                 m_color.header.c_str(), m_color.border.c_str());
                line.clear();
                line << left << m_color.border << "\xe2\x94\x8c";       // ┌
                line << horzline;                                       // ─
                line << "\xe2\x94\x90" << "\x1b[m";                     // ┐
                if (clear_eol && _rl_term_clreol)
                    line << _rl_term_clreol;
                m_printer->print(line.c_str(), line.length());
            }

#ifdef SHOW_VERT_SCROLLBARS
            const int32 car_top = calc_scroll_car_offset(m_top, m_visible_rows, count, m_vert_scroll_car);
            m_vert_scroll_column = m_mouse_left + m_mouse_width;
#endif

            // Display items.
            for (int32 row = 0; row < m_visible_rows; row++)
            {
                const int32 i = m_top + row;
                if (i >= count)
                    break;

                rl_crlf();
                up++;

                move_to_end = true;
                if (m_prev_displayed < 0 ||
                    i == m_index ||
                    i == m_prev_displayed)
                {
                    line.clear();
                    line << left << m_color.border << "\xe2\x94\x82";   // │

                    const str_base& maincolor = (i == m_index) ? m_color.select : m_color.items;
                    line << maincolor;

                    int32 spaces = content_width;
                    int32 item_spaces = m_item_cells ? m_item_cells : spaces;

                    if (m_show_numbers)
                    {
                        const int32 history_index = m_infos ? get_item_info(i).index : get_original_index(i);
                        const char ismark = (m_infos && get_item_info(i).marked);
                        const char mark = ismark ? '*' : ' ';
                        const char* color = !ismark ? "" : (i == m_index) ? m_color.selectmark.c_str() : m_color.mark.c_str();
                        const char* uncolor = !ismark ? "" : (i == m_index) ? m_color.select.c_str() : m_color.items.c_str();
                        tmp.clear();
                        tmp.format("%*u:%s%c", max<>(0, m_max_num_cells - 2), history_index + 1, color, mark);
                        line << tmp;                                    // history number
                        const uint32 number_cells = cell_count(tmp.c_str());
                        if (spaces > number_cells)
                            spaces -= number_cells;
                        else
                            spaces = 0;
                        // NOTE: item_spaces already accounts for m_max_num_cells.
                    }

                    int32 cell_len;
                    const char* item_text;
                    const int32 char_len = limit_cells(get_item_text(i), item_spaces, cell_len, m_horz_offset * m_horz_item_enabled, &tmp, &item_text);
                    line.concat(item_text, char_len);                   // main text
                    spaces -= cell_len;

                    if (m_has_columns)
                    {
                        const str_base& desc_color = (i == m_index) ? m_color.selectdesc : m_color.desc;

                        if (m_columns.get_any_tabs())
                        {
                            const uint32 pad_to = (m_item_cells ? m_item_cells : m_longest) - cell_len;
                            make_spaces(min<int32>(spaces, pad_to), tmp);
                            line << tmp;                                // spaces
                            spaces -= tmp.length();
                        }

                        bool first_col = true;
                        for (int32 col = 0; col < max_columns && spaces > 0; col++)
                        {
                            const int32 col_width = m_columns.get_col_layout_width(col);
                            if (col_width <= 0)
                                continue;

                            static_assert(col_padding <= 2, "col_padding is wider than the static spaces string");
                            line.concat("  ", min<int32>(col_padding, spaces));
                            spaces -= min<int32>(col_padding, spaces);

                            const char* col_text = get_col_text(i, col);
                            const int32 col_len = limit_cells(col_text ? col_text : "", spaces, cell_len, m_horz_offset * m_horz_column_enabled[col], &tmp, &col_text);

                            if (first_col)
                            {
                                first_col = false;
                                if (!desc_color.empty())
                                    line << desc_color;
                            }
                            line.concat(col_text, col_len);             // column text
                            spaces -= cell_len;

                            int32 pad = min<int32>(spaces, col_width - cell_len);
                            if (pad > 0)
                            {
                                make_spaces(pad, tmp);
                                line << tmp;                            // spaces
                                spaces -= tmp.length();
                            }
                        }
                    }

                    make_spaces(spaces, tmp);
                    line << tmp;                                        // spaces

                    line << m_color.border;
#ifdef SHOW_VERT_SCROLLBARS
                    const char* car = get_scroll_car_char(row, car_top, m_vert_scroll_car, false/*floating*/);
                    if (car)
                        line << car;                                    // ┃ or etc
                    else
#endif
                        line << "\xe2\x94\x82";                         // │
                    line << "\x1b[m";
                    if (clear_eol && _rl_term_clreol)
                        line << _rl_term_clreol;
                    m_printer->print(line.c_str(), line.length());
                }
            }

            // Display border.
            if (draw_border)
            {
                rl_crlf();
                up++;
                make_horz_border(footer.empty() ? nullptr : footer.c_str(), content_width, true/*bars*/, horzline, m_color.footer.c_str(), m_color.border.c_str());
                line.clear();
                line << left << m_color.border << "\xe2\x94\x94";       // └
                line << horzline;                                       // ─
                line << "\xe2\x94\x98" << "\x1b[m";                     // ┘
                if (clear_eol && _rl_term_clreol)
                    line << _rl_term_clreol;
                m_printer->print(line.c_str(), line.length());
            }

            if (m_force_clear)
                m_printer->print("\x1b[m\x1b[J");

            m_prev_displayed = m_index;
        }
        else
        {
            // Clear to end of screen.
            m_printer->print("\x1b[m\x1b[J");

            m_prev_displayed = -1;
        }

        m_force_clear = false;

        // Restore cursor position.
        if (up > 0)
        {
            str<16> s;
            s.format("\x1b[%dA", up);
            m_printer->print(s.c_str(), s.length());
        }
        GetConsoleScreenBufferInfo(h, &csbi);
        m_mouse_offset = csbi.dwCursorPosition.Y + 1/*to top item*/;
        if (!s_standalone)
        {
            m_mouse_offset += 1/*to border*/;
            _rl_move_vert(vpos);
            _rl_last_c_pos = cpos;
            GetConsoleScreenBufferInfo(h, &csbi);
        }
        restore.Y = csbi.dwCursorPosition.Y;
        SetConsoleCursorPosition(h, restore);
    }
}

//------------------------------------------------------------------------------
void textlist_impl::set_top(int32 top)
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
void textlist_impl::adjust_horz_offset(int32 delta)
{
    const int32 was = m_horz_offset;

    m_horz_offset += delta;
    m_horz_offset = min<int32>(m_horz_offset, m_horz_scroll_range);
    m_horz_offset = max<int32>(m_horz_offset, 0);

    if (was != m_horz_offset)
    {
        m_prev_displayed = -1;
        update_display();
    }
}

//------------------------------------------------------------------------------
static void init_color(const str_base* first, const char* second, str_base& out)
{
    if (first && first->length())
        out = first->c_str();
    else
        out = second;
}

//------------------------------------------------------------------------------
static void wrap_color(str_base& color)
{
    str<64> tmp;
    tmp = color.c_str();
    color.format("\x1b[%sm", tmp.c_str());
}

//------------------------------------------------------------------------------
void textlist_impl::init_colors(const popup_config* config)
{
    init_color(config ? &config->colors.items : nullptr, get_popup_colors(), m_color.items);
    init_color(config ? &config->colors.desc : nullptr, get_popup_desc_colors(), m_color.desc);
    init_color(config ? &config->colors.border : nullptr, get_popup_border_colors(config && !config->colors.items.empty() ? m_color.items.c_str() : nullptr), m_color.border);
    init_color(config ? &config->colors.header : nullptr, get_popup_header_colors(config && (!config->colors.items.empty() || !config->colors.border.empty()) ? m_color.border.c_str() : nullptr), m_color.header);
    init_color(config ? &config->colors.footer : nullptr, get_popup_footer_colors(config && (!config->colors.items.empty() || !config->colors.border.empty()) ? m_color.border.c_str() : nullptr), m_color.footer);
    init_color(config ? &config->colors.select : nullptr, get_popup_select_colors(config && !config->colors.items.empty() ? m_color.items.c_str() : nullptr), m_color.select);
    init_color(config ? &config->colors.selectdesc : nullptr, get_popup_selectdesc_colors(config && (!config->colors.items.empty() || !config->colors.select.empty()) ? m_color.select.c_str() : nullptr), m_color.selectdesc);
    init_color(config ? &config->colors.mark : nullptr, m_color.desc.c_str(), m_color.mark);

    wrap_color(m_color.items);
    wrap_color(m_color.desc);
    wrap_color(m_color.border);
    wrap_color(m_color.header);
    wrap_color(m_color.footer);
    wrap_color(m_color.select);
    wrap_color(m_color.selectdesc);
    wrap_color(m_color.mark);

    // Not supported yet; the mark is only used internally.
    m_color.selectmark.clear();
}

//------------------------------------------------------------------------------
void textlist_impl::reset()
{
    // Don't reset screen row and cols; they stay in sync with the terminal.

    m_visible_rows = 0;
    m_max_num_cells = 0;
    m_item_cells = 0;
    m_longest_visible = 0;
    m_horz_offset = 0;
    m_horz_item_enabled = 0;
    memset(m_horz_column_enabled, 0, sizeof(m_horz_column_enabled));
    m_horz_scroll_range = 0;
    m_default_title.clear();
    m_override_title.clear();
    m_has_override_title = false;
    m_force_clear = false;

    m_count = 0;
    m_entries = nullptr;    // Don't free; is only borrowed.
    m_infos = nullptr;      // Don't free; is only borrowed.
    m_items = std::move(std::vector<const char*>());
    m_longest = 0;
    m_columns.clear();

    m_filter_string.clear();
    m_filter_saved_index = -1;
    m_filter_saved_top = -1;
    m_original_count = 0;
    m_filtered_items = std::move(std::vector<int32>());

    m_mode = textlist_mode::general;
    m_pref_height = 0;
    m_pref_width = 0;
    m_history_mode = false;
    m_show_numbers = false;
    m_win_history = false;
    m_has_columns = false;
    m_del_callback = nullptr;

    m_prev_content_width = 0;

    m_top = 0;
    m_index = 0;
    m_prev_displayed = -1;

    m_needle.clear();
    m_needle_is_number = false;
    m_input_clears_needle = false;

    m_store.clear();
}

//------------------------------------------------------------------------------
int32 textlist_impl::get_original_index(int32 index) const
{
    if (!m_filter_string.empty())
        index = m_filtered_items[index];
    return index;
}

//------------------------------------------------------------------------------
const char* textlist_impl::get_item_text(int32 index) const
{
    if (!m_filter_string.empty())
        index = m_filtered_items[index];
    return m_items[index];
}

//------------------------------------------------------------------------------
const char* textlist_impl::get_col_text(int32 index, int32 col) const
{
    if (!m_filter_string.empty())
        index = m_filtered_items[index];
    return m_columns.get_col_text(index, col);
}

//------------------------------------------------------------------------------
const entry_info& textlist_impl::get_item_info(int32 index) const
{
    assert(m_infos);
    if (!m_filter_string.empty())
        index = m_filtered_items[index];
    return m_infos[index];
}

//------------------------------------------------------------------------------
void textlist_impl::clear_filter()
{
    if (!m_filter_string.empty())
    {
        m_count = m_original_count;
        m_filter_string.clear();
        m_filtered_items.clear();
        m_index = m_filter_saved_index;
        set_top(m_filter_saved_top);
#ifdef SHOW_VERT_SCROLLBARS
        m_vert_scroll_car = calc_scroll_car_size(m_visible_rows, m_count);
#endif
    }
}

//------------------------------------------------------------------------------
bool textlist_impl::filter_items()
{
    assert(!m_needle_is_number);

    if (m_filter_string.equals(m_needle.c_str()))
        return false;

    if (m_needle.empty())
    {
        clear_filter();
        return true;
    }

    int32 mode = g_ignore_case.get();
    if (mode < 0 || mode >= str_compare_scope::num_scope_values)
        mode = str_compare_scope::exact;
    str_compare_scope _(mode, g_fuzzy_accent.get());

    int32 defer_test = 0;
    int32 tested = 0;
    auto test_input = [&](){
        ++tested;
        defer_test = 128;
        if (!m_dispatcher.available(0))
            return false;
        const uint8 c = m_dispatcher.peek();
        if (!c)
            return false;
        if (c != 0x08 && (c < ' ' || c >= 0xf8))
        {
            defer_test = 999999;
            return false;
        }
        return true;
    };

    // Build new filtered list.
    std::vector<int32> filtered_items;
    if (!m_filter_string.empty() && strncmp(m_needle.c_str(), m_filter_string.c_str(), m_filter_string.length()) == 0)
    {
        // Further filter the filtered list.
        for (size_t i = 0; i < m_filtered_items.size(); ++i)
        {
            // Interrupt if more input is available.
            if (!defer_test-- && test_input())
                return false;

            const int32 original_index = m_filtered_items[i];

            bool match = m_needle.empty() || strstr_compare(m_needle, m_items[original_index]);
            if (m_has_columns)
            {
                for (int32 col = 0; !match && col < max_columns; col++)
                    match = strstr_compare(m_needle, m_columns.get_col_text(original_index, col));
            }

            if (match)
                filtered_items.push_back(original_index);
        }
    }
    else
    {
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            // Interrupt if more input is available.
            if (!defer_test-- && test_input())
                return false;

            bool match = m_needle.empty() || strstr_compare(m_needle, m_items[i]);
            if (m_has_columns)
            {
                for (int32 col = 0; !match && col < max_columns; col++)
                    match = strstr_compare(m_needle, m_columns.get_col_text(i, col));
            }

            if (match)
                filtered_items.push_back(int32(i));
        }
    }

    // Swap new filtered list into place.
    m_filtered_items = std::move(filtered_items);
    m_count = int32(m_filtered_items.size());

    // Save selected item if no filtered applied yet.
    if (m_filter_string.empty())
    {
        m_filter_saved_index = m_index;
        m_filter_saved_top = m_top;
    }

    // Remember the filter string.
    m_filter_string = m_needle.c_str();

    // Reset the selected item.
    if (m_reverse)
        m_index = max(0, m_count - 1);
    else
        m_index = 0;
    set_top(0);
    update_top();

#ifdef SHOW_VERT_SCROLLBARS
    // Update the size of the scroll bar, since m_count may have changed.
    m_vert_scroll_car = calc_scroll_car_size(m_visible_rows, m_count);
#endif

    return true;
}



//------------------------------------------------------------------------------
textlist_impl::item_store::~item_store()
{
    clear();
}

//------------------------------------------------------------------------------
const char* textlist_impl::item_store::add(const char* item)
{
    unsigned len = unsigned(strlen(item) + 1);

    if (len > m_back - m_front)
    {
        page* p = (page*)VirtualAlloc(nullptr, pagesize, MEM_COMMIT, PAGE_READWRITE);
        p->next = m_page;
        m_page = p;
        m_front = &p->data - (const char*)p;
        m_back = 65536;
    }

    char* p = reinterpret_cast<char*>(m_page) + m_front;
    memcpy(p, item, len);
    m_front += len;
    return p;
}

//------------------------------------------------------------------------------
void textlist_impl::item_store::clear()
{
    while (m_page)
    {
        page* p = m_page;
        m_page = p->next;
        VirtualFree(p, 0, MEM_RELEASE);
    }

    m_front = m_back = 0;
}



//------------------------------------------------------------------------------
popup_results activate_text_list(const char* title, const char** entries, int32 count, int32 current, bool has_columns, const popup_config* config)
{
    if (!s_textlist)
        return popup_result::error;

    return s_textlist->activate(title, entries, count, current, false/*reverse*/, textlist_mode::general, nullptr, has_columns, config);
}

//------------------------------------------------------------------------------
popup_results activate_directories_text_list(const char** dirs, int32 count)
{
    if (!s_textlist)
        return popup_result::error;

    return s_textlist->activate("Directories", dirs, count, count - 1, true/*reverse*/, textlist_mode::directories, nullptr, false);
}

//------------------------------------------------------------------------------
popup_results activate_history_text_list(const char** history, int32 count, int32 current, entry_info* infos, bool win_history)
{
    if (!s_textlist)
        return popup_result::error;

    assert(current >= 0);
    assert(current < count);
    textlist_mode mode = win_history ? textlist_mode::win_history : textlist_mode::history;
    return s_textlist->activate("History", history, count, current, true/*reverse*/, mode, infos, false);
}



//------------------------------------------------------------------------------
class standalone_input : public input_dispatcher, public key_tester
{
    typedef editor_module                       module;
    typedef fixed_array<editor_module*, 16>     modules;

public:
                        standalone_input(terminal& term);

    // input_dispatcher
    void                dispatch(int32 bind_group) override;
    bool                available(uint32 timeout) override;
    uint8               peek() override;

    // key_tester
    bool                is_bound(const char* seq, int32 len);
    bool                accepts_mouse_input(mouse_input_type type);
    bool                translate(const char* seq, int32 len, str_base& out);

private:
    module::context     get_context();
    bool                update_input();

    terminal&           m_terminal;
    modules             m_modules;
    binder              m_binder;
    bind_resolver       m_bind_resolver = { m_binder };
    textlist_impl       m_textlist;

    // State for dispatch().
    uint8               m_dispatching = 0;
    bool                m_invalid_dispatch = false;
    bind_resolver::binding* m_pending_binding = nullptr;
};

//------------------------------------------------------------------------------
standalone_input::standalone_input(terminal& term)
: m_terminal(term)
, m_textlist(*this)
{
    *m_modules.push_back() = &m_textlist;

    struct : public module::binder {
        virtual int32 get_group(const char* name) const override
        {
            return binder->get_group(name);
        }

        virtual int32 create_group(const char* name) override
        {
            return binder->create_group(name);
        }

        virtual bool bind(uint32 group, const char* chord, uint8 key, bool has_params=false) override
        {
            return binder->bind(group, chord, *module, key, has_params);
        }

        ::binder*       binder;
        module*         module;
    } binder_impl;

    binder_impl.binder = &m_binder;
    for (auto* module : m_modules)
    {
        binder_impl.module = module;
        module->bind_input(binder_impl);
    }

    module::context context = get_context();
    for (auto* module : m_modules)
        module->on_begin_line(context);
}

//------------------------------------------------------------------------------
void standalone_input::dispatch(int32 bind_group)
{
    // Claim any pending binding, otherwise we'll try to dispatch it again.

    if (m_pending_binding)
        m_pending_binding->claim();

    // Handle one input.

    const int32 prev_bind_group = m_bind_resolver.get_group();
    m_bind_resolver.set_group(bind_group);

    const bool was_signaled = clink_is_signaled();

    m_dispatching++;

    key_tester* const old_key_tester = m_terminal.in->set_key_tester(this);

    do
    {
        m_terminal.in->select();
        m_invalid_dispatch = false;
    }
    while (!update_input() || m_invalid_dispatch);

    m_terminal.in->set_key_tester(old_key_tester);

    m_dispatching--;

    m_bind_resolver.set_group(prev_bind_group);
}

//------------------------------------------------------------------------------
bool standalone_input::available(uint32 timeout)
{
    return m_terminal.in->available(timeout);
}

//------------------------------------------------------------------------------
uint8 standalone_input::peek()
{
    const int32 c = m_terminal.in->peek();
    assert(c < 0xf8);
    return (c < 0) ? 0 : uint8(c);
}

//------------------------------------------------------------------------------
editor_module::context standalone_input::get_context()
{
    assert(g_printer);
    module::context context = { nullptr, nullptr, *g_printer, *(pager*)nullptr, *(line_buffer*)nullptr, *(matches*)nullptr, *(word_classifications*)nullptr, *(input_hint*)nullptr };
    return context;
}

//------------------------------------------------------------------------------
// Returns false when a chord is in progress, otherwise returns true.  This is
// to help dispatch() be able to dispatch an entire chord.
bool standalone_input::update_input()
{
    // Signal handler is not installed when standalone.
    const int32 sig = clink_is_signaled();
    if (sig)
    {
        for (auto* module : m_modules)
            module->on_signal(sig);
        if (!m_dispatching)
            exit(-1);
        return true;
    }

    int32 key = m_terminal.in->read();

    if (key == terminal_in::input_terminal_resize)
    {
        int32 columns = m_terminal.out->get_columns();
        int32 rows = m_terminal.out->get_rows();
        module::context context = get_context();
        for (auto* module : m_modules)
            module->on_terminal_resize(columns, rows, context);
    }

    if (key == terminal_in::input_abort)
    {
#if 0
        if (!m_dispatching)
        {
            m_buffer.reset();
            end_line();
        }
#endif
        return true;
    }

    if (key == terminal_in::input_exit)
    {
#if 0
        if (!m_dispatching)
        {
            m_buffer.reset();
            m_buffer.insert("exit 0");
            end_line();
        }
#endif
        return true;
    }

    if (key < 0)
        return true;

    if (!m_bind_resolver.step(key))
        return false;

    struct result_impl : public module::result
    {
        enum
        {
            flag_pass       = 1 << 0,
            flag_invalid    = 1 << 1,
            flag_done       = 1 << 2,
            flag_eof        = 1 << 3,
            flag_redraw     = 1 << 4,
        };

        virtual void    pass() override                           { flags |= flag_pass; }
        virtual void    loop() override                           { flags |= flag_invalid; }
        virtual void    done(bool eof) override                   { flags |= flag_done|(eof ? flag_eof : 0); }
        virtual void    redraw() override                         { flags |= flag_redraw; }
        virtual int32   set_bind_group(int32 id) override         { int32 t = group; group = id; return t; }
        unsigned short  group;  //        <! MSVC bugs; see connect
        uint8           flags;  // = 0;   <! issues about C2905
    };

    while (auto binding = m_bind_resolver.next())
    {
        // Binding found, dispatch it off to the module.
        result_impl result;
        result.flags = 0;
        result.group = m_bind_resolver.get_group();

        str<16> chord;
        module* module = binding.get_module();
        uint8 id = binding.get_id();
        binding.get_chord(chord);

        {
            rollback<bind_resolver::binding*> _(m_pending_binding, &binding);

            module::context context = get_context();
            module::input input = { chord.c_str(), chord.length(), id, m_bind_resolver.more_than(chord.length()), binding.get_params() };
            module->on_input(input, result, context);

            if (clink_is_signaled())
                return true;
        }

        m_bind_resolver.set_group(result.group);

        // Process what result_impl has collected.

        if (binding) // May have been claimed already by dispatch() inside on_input().
        {
            if (result.flags & result_impl::flag_pass)
            {
                // win_terminal_in avoids producing input that won't be handled.
                // But it can't predict when result.pass() might be used, so the
                // onus is on the pass() caller to make sure passing the binding
                // upstream won't leave it unhandled.  If it's unhandled, then
                // the key sequence gets split at the point of mismatch, and the
                // rest gets interpreted as a separate key sequence.
                //
                // For example, mouse input can be especially susceptible.
                continue;
            }
            binding.claim();
        }

        if (m_dispatching)
        {
            if (result.flags & result_impl::flag_invalid)
                m_invalid_dispatch = true;
        }
        else
        {
            // There's no "done".
#if 0
            if (result.flags & result_impl::flag_done)
            {
                end_line();

                if (result.flags & result_impl::flag_eof)
                    set_flag(flag_eof);
            }
#endif

            // There's no editing.
#if 0
            if (!check_flag(flag_editing))
                return true;
#endif
        }

        // There's nothing to draw.
#if 0
        if (result.flags & result_impl::flag_redraw)
            m_buffer.redraw();
#endif
    }

    // There's nothing to draw.
#if 0
    m_buffer.draw();
#endif
    return true;
}

//------------------------------------------------------------------------------
bool standalone_input::is_bound(const char* seq, int32 len)
{
    int32 bound = m_bind_resolver.is_bound(seq, len);
    if (bound != 0)
        return (bound > 0);

    return false;
}

//------------------------------------------------------------------------------
bool standalone_input::accepts_mouse_input(mouse_input_type type)
{
    if (m_textlist.is_active())
        return m_textlist.accepts_mouse_input(type);
    return false;
}

//------------------------------------------------------------------------------
bool standalone_input::translate(const char* seq, int32 len, str_base& out)
{
    return false;
}

//------------------------------------------------------------------------------
void init_standalone_textlist(terminal& term)
{
    // This initializes s_textlist.
    s_standalone = true;
    new standalone_input(term);

    // Since there is no inputrc file in standalone mode, set some defaults.
    _rl_menu_complete_wraparound = false;   // Affects textlist_impl.
}
