// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "textlist_impl.h"
#include "binder.h"
#include "editor_module.h"
#include "line_buffer.h"
#include "terminal_helpers.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str_compare.h>
#include <core/str_iter.h>
#include <rl/rl_commands.h>
#include <terminal/printer.h>
#include <terminal/ecma48_iter.h>

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
    bind_id_textlist_pgup,
    bind_id_textlist_pgdn,
    bind_id_textlist_home,
    bind_id_textlist_end,
    bind_id_textlist_findnext,
    bind_id_textlist_findprev,
    bind_id_textlist_copy,
    bind_id_textlist_escape,
    bind_id_textlist_enter,
    bind_id_textlist_insert,

    bind_id_textlist_catchall = binder::id_catchall_only_printable,
};

//------------------------------------------------------------------------------
extern setting_enum g_ignore_case;
extern setting_bool g_fuzzy_accent;
extern const char* get_popup_colors();

//------------------------------------------------------------------------------
static textlist_impl* s_textlist = nullptr;
const int min_screen_cols = 20;

//------------------------------------------------------------------------------
static int make_item(const char* in, str_base& out)
{
    out.clear();

    int cells = 0;
    for (str_iter iter(in, strlen(in)); int c = iter.next(); in = iter.get_pointer())
    {
        if (unsigned(c) < ' ')
        {
            char ctrl = char(c) + '@';
            out.concat("^", 1);
            out.concat(&ctrl, 1);
            cells += 2;
        }
        else
        {
            out.concat(in, int(iter.get_pointer() - in));
            cells += clink_wcwidth(c);
        }
    }
    return cells;
}

//------------------------------------------------------------------------------
static int limit_cells(const char* in, int limit, int& cells)
{
    cells = 0;
    str_iter iter(in, strlen(in));
    while (int c = iter.next())
    {
        cells += clink_wcwidth(c);
        if (cells >= limit)
            break;
    }
    return int(iter.get_pointer() - in);
}

//------------------------------------------------------------------------------
textlist_impl::textlist_impl(input_dispatcher& dispatcher)
    : m_dispatcher(dispatcher)
{
}

//------------------------------------------------------------------------------
popup_results textlist_impl::activate(const char* title, const char** entries, int count, bool history_mode)
{
    reset();
    m_results.clear();

    assert(m_buffer);
    if (!m_buffer)
        return popup_result::error;

    if (!entries || count <= 0)
        return popup_result::error;

    // Doesn't make sense to record macro with a popup list.
    if (RL_ISSTATE(RL_STATE_MACRODEF) != 0)
        return popup_result::error;

    // Make sure there's room.
    m_history_mode = history_mode;
    update_layout();
    if (m_visible_rows <= 0)
    {
        m_history_mode = false;
        return popup_result::error;
    }

    // Gather the items.
    str<> tmp;
    m_entries = entries;
    m_count = count;
    for (int i = 0; i < count; i++)
    {
        m_longest = max<int>(m_longest, make_item(m_entries[i], tmp));
        m_items.push_back(m_store.add(tmp.c_str()));
    }

    if (title && *title)
        m_default_title.format(" %s ", title);
    m_history_mode = history_mode;

    // Initialize the view.
    m_index = count - 1;
    m_top = max<int>(0, count - m_visible_rows);

    show_cursor(false);
    lock_cursor(true);

    assert(!m_active);
    m_active = true;
    update_display();

    m_dispatcher.dispatch(m_bind_group);

    // Cancel if the dispatch loop is left unexpectedly (e.g. certain errors).
    if (m_active)
        cancel(popup_result::cancel);

    assert(!m_active);
    update_display();

    _rl_refresh_line();
    rl_display_fixed = 1;

    lock_cursor(false);
    show_cursor(true);

    popup_results results;
    results.m_result = m_results.m_result;
    results.m_index = m_results.m_index;
    results.m_text = std::move(m_results.m_text);

    reset();
    m_results.clear();

    return results;
}

//------------------------------------------------------------------------------
void textlist_impl::bind_input(binder& binder)
{
    const char* esc = get_bindable_esc();

    m_bind_group = binder.create_group("textlist");
    binder.bind(m_bind_group, "\\e[A", bind_id_textlist_up);
    binder.bind(m_bind_group, "\\e[B", bind_id_textlist_down);
    binder.bind(m_bind_group, "\\e[5~", bind_id_textlist_pgup);
    binder.bind(m_bind_group, "\\e[6~", bind_id_textlist_pgdn);
    binder.bind(m_bind_group, "\\e[H", bind_id_textlist_home);
    binder.bind(m_bind_group, "\\e[F", bind_id_textlist_end);
    binder.bind(m_bind_group, "\\eOR", bind_id_textlist_findnext);
    binder.bind(m_bind_group, "\\e[1;2R", bind_id_textlist_findprev);
    binder.bind(m_bind_group, "\\r", bind_id_textlist_enter);
    binder.bind(m_bind_group, "^i", bind_id_textlist_insert);
    binder.bind(m_bind_group, "\\e[27;5;73~", bind_id_textlist_insert);
    binder.bind(m_bind_group, "^c", bind_id_textlist_copy);

    binder.bind(m_bind_group, "^g", bind_id_textlist_escape);
    if (esc)
        binder.bind(m_bind_group, esc, bind_id_textlist_escape);

    binder.bind(m_bind_group, "", bind_id_textlist_catchall);
}

//------------------------------------------------------------------------------
void textlist_impl::on_begin_line(const context& context)
{
    assert(!s_textlist);
    s_textlist = this;
    m_buffer = &context.buffer;
    m_printer = &context.printer;

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
void textlist_impl::on_input(const input& _input, result& result, const context& context)
{
    assert(m_active);

    input input = _input;
    bool set_input_clears_needle = true;
    bool from_top = false;

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
            const int y = m_index;
            const int rows = min<int>(m_count, m_visible_rows);

            // Use rows as the page size (vs the more common rows-1) for
            // compatibility with Conhost's F7 popup list behavior.
            if (input.id == bind_id_textlist_pgup)
            {
                if (y > 0)
                {
                    int new_y = max<int>(0, (y == m_top) ? y - rows : m_top);
                    m_index += (new_y - y);
                    goto navigated;
                }
            }
            else if (input.id == bind_id_textlist_pgdn)
            {
                if (y < m_count - 1)
                {
                    int bottom_y = m_top + rows - 1;
                    int new_y = min<int>(m_count - 1, (y == bottom_y) ? y + rows : bottom_y);
                    m_index += (new_y - y);
                    if (m_index > m_count - 1)
                    {
                        set_top(max<int>(0, m_count - m_visible_rows));
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
        if (m_history_mode)
            break;
find:
        if (m_history_mode)
        {
            lock_cursor(false);
            show_cursor(true);
            rl_ding();
            show_cursor(false);
            lock_cursor(true);
        }
        else
        {
            int direction = (input.id == bind_id_textlist_findnext) ? 1 : -1;

            int mode = g_ignore_case.get();
            if (mode < 0 || mode >= str_compare_scope::num_scope_values)
                mode = str_compare_scope::exact;
            str_compare_scope _(mode, g_fuzzy_accent.get());

            int i = m_index;
            if (direction < 0)
            {
                if (from_top)
                    i = m_count;
                i--;
            }
            else
            {
                if (from_top)
                    i = 0;
            }

            int original = i;
            while (true)
            {
                int cmp = str_compare(m_needle.c_str(), m_items[i]);
                if (cmp == -1 || cmp == m_needle.length())
                {
                    m_index = i;
                    if (m_index < m_top || m_index >= m_top + m_visible_rows)
                        m_top = max<int>(0, min<int>(m_index, m_count - m_visible_rows));
                    m_prev_displayed = -1;
                    update_display();
                    break;
                }

                i += direction;
                if (direction < 0)
                {
                    if (i < 0)
                        i = m_count - 1;
                }
                else
                {
                    if (i >= m_count)
                        i = 0;
                }
                if (i == m_index)
                    break;
            }
        }
        break;

    case bind_id_textlist_copy:
        {
            const char* text = m_entries[m_index];
            os::set_clipboard_text(text, int(strlen(text)));
            set_input_clears_needle = false;
        }
        break;

    case bind_id_textlist_escape:
        cancel(popup_result::cancel);
        return;

    case bind_id_textlist_enter:
        cancel(popup_result::use);
        return;

    case bind_id_textlist_insert:
        cancel(popup_result::select);
        return;

    case bind_id_textlist_catchall:
        {
            bool refresh = false;
            bool ignore = false;

            set_input_clears_needle = false;

            if (input.len == 1 && input.keys[0] == 8)
            {
                if (!m_needle.length())
                    break;
                int point = _rl_find_prev_mbchar(const_cast<char*>(m_needle.c_str()), m_needle.length(), MB_FIND_NONZERO);
                m_needle.truncate(point);
                from_top = !m_history_mode;
                refresh = true;
                goto update_needle;
            }

            // Figure out whether to ignore the input.
            {
                str_iter iter(input.keys, input.len);
                while (iter.more())
                {
                    unsigned int c = iter.next();
                    if (c < ' ' || c == 0x7f)
                    {
                        ignore = true;
                        break;
                    }
                }
            }

            if (ignore)
                break;

            // Collect the input.
            {
                if (m_input_clears_needle)
                {
                    assert(!m_history_mode);
                    m_needle.clear();
                    m_needle_is_number = false;
                    m_input_clears_needle = false;
                }

                str_iter iter(input.keys, input.len);
                const char* seq = iter.get_pointer();
                while (iter.more())
                {
                    unsigned int c = iter.next();
                    if (!m_history_mode)
                    {
                        refresh = m_has_override_title;
                        m_override_title.clear();
                        m_needle.concat(seq, int(iter.get_pointer() - seq));
                    }
                    else if (c >= '0' && c <= '9')
                    {
                        if (!m_needle_is_number)
                        {
                            refresh = m_has_override_title;
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
                        refresh = m_has_override_title;
                        m_override_title.clear();
                        m_needle.clear();
                        m_needle.concat(seq, int(iter.get_pointer() - seq));
                        m_needle_is_number = false;
                    }
                    seq = iter.get_pointer();
                }
            }

            // Handle the input.
update_needle:
            if (!m_history_mode)
            {
                input.id = bind_id_textlist_findnext;
                goto find;
            }
            else if (m_needle_is_number)
            {
                if (m_needle.length())
                {
                    refresh = true;
                    m_override_title.clear();
                    m_override_title.format("\xe2\x94\xa4 enter history number: %-6s \xe2\x94\x9c", m_needle.c_str());
                    int i = atoi(m_needle.c_str()) - 1;
                    if (i >= 0 && i < m_count)
                    {
                        m_index = i;
                        if (m_index < m_top || m_index >= m_top + m_visible_rows)
                            m_top = max<int>(0, min<int>(m_index - (m_visible_rows / 2), m_count - m_visible_rows));
                        m_prev_displayed = -1;
                        refresh = true;
                    }
                }
                else if (m_override_title.length())
                {
                    refresh = true;
                    m_override_title.clear();
                }
            }
            else if (m_needle.length())
            {
                str_compare_scope _(str_compare_scope::caseless, true/*fuzzy_accent*/);

                int i = m_index;
                while (true)
                {
                    i--;
                    if (i < 0)
                        i = m_count - 1;
                    if (i == m_index)
                        break;

                    int cmp = str_compare(m_needle.c_str(), m_items[i]);
                    if (cmp == -1 || cmp == m_needle.length())
                    {
                        m_index = i;
                        if (m_index < m_top || m_index >= m_top + m_visible_rows)
                            m_top = max<int>(0, min<int>(m_index, m_count - m_visible_rows));
                        m_prev_displayed = -1;
                        refresh = true;
                        break;
                    }
                }
            }

            if (refresh)
                update_display();
        }
        break;
    }

    if (set_input_clears_needle && !m_history_mode)
        m_input_clears_needle = true;

    // Keep dispatching input.
    result.loop();
}

//------------------------------------------------------------------------------
void textlist_impl::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
}

//------------------------------------------------------------------------------
void textlist_impl::on_terminal_resize(int columns, int rows, const context& context)
{
    m_screen_cols = columns;
    m_screen_rows = rows;

    if (m_active)
        cancel(popup_result::cancel);
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
            m_results.m_index = m_index;
            m_results.m_text = m_entries[m_index];
        }
    }

    m_active = false;
}

//------------------------------------------------------------------------------
void textlist_impl::update_layout()
{
    int slop_rows = 2;
    int border_rows = 2;
    int target_rows = m_history_mode ? 20 : 10;

    m_visible_rows = min<int>(target_rows, (m_screen_rows / 2) - border_rows - slop_rows);

    if (m_screen_cols <= min_screen_cols)
        m_visible_rows = 0;
}

//------------------------------------------------------------------------------
void textlist_impl::update_top()
{
    const int y = m_index;
    if (m_top > y)
    {
        set_top(y);
    }
    else
    {
        const int rows = min<int>(m_count, m_visible_rows);
        int top = max<int>(0, y - (rows - 1));
        if (m_top < top)
            set_top(top);
    }
    assert(m_top >= 0);
    assert(m_top <= max<int>(0, m_count - m_visible_rows));
}

//------------------------------------------------------------------------------
void textlist_impl::update_display()
{
    if (m_visible_rows > 0)
    {
        // Remember the cursor position so it can be restored later to stay
        // consistent with Readline's view of the world.
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(h, &csbi);
        COORD restore = csbi.dwCursorPosition;
        const int vpos = _rl_last_v_pos;
        const int cpos = _rl_last_c_pos;

        // Move cursor to next line.  I.e. the list goes immediately below the
        // cursor line and may overlay some lines of input.
        m_printer->print("\n");

        // Display list.
        int up = 1;
        bool move_to_end = true;
        const int count = m_count;
        if (m_active && count > 0)
        {
            update_top();

            const bool draw_border = (m_prev_displayed < 0) || m_override_title.length() || m_has_override_title;
            m_has_override_title = !m_override_title.empty();

            str<> tmp;
            int max_num_len = 0;
            if (m_history_mode)
            {
                tmp.format("%u", m_count);
                max_num_len = tmp.length();
            }

            const int longest = max<int>(m_longest + (max_num_len ? max_num_len + 2 : 0), 40); // +2 for ": ".
            const int effective_screen_cols = (m_screen_cols < 40) ? m_screen_cols : max<int>(40, m_screen_cols - 8);
            const int col_width = min<int>(longest + 2, effective_screen_cols); // +2 for borders.

            str<> noescape;
            str<> left;
            str<> horzline;
            for (int i = col_width - 2; i--;)
                horzline.concat("\xe2\x94\x80", 3);

            {
                int x = (m_screen_cols - col_width) / 2;
                if (x > 0)
                    left.format("\x1b[%uG", x + 1);
            }

            str<32> color;
            {
                const char* _color = get_popup_colors();
                color.format("\x1b[%sm", _color);
            }

            // Display border.
            if (draw_border)
            {
                const str_base* topline = &horzline;
                if (m_default_title.length() || m_override_title.length())
                {
                    str_base* title = m_has_override_title ? &m_override_title : &m_default_title;
                    int title_cells = cell_count(title->c_str());
                    int x = (col_width - 2 - title_cells) / 2;
                    tmp.clear();
                    x--;
                    for (int i = x; i-- > 0;)
                        tmp.concat("\xe2\x94\x80", 3);
                    x += title_cells;
                    tmp.concat(title->c_str(), title->length());
                    for (int i = col_width - 2 - x; i-- > 0;)
                        tmp.concat("\xe2\x94\x80", 3);
                    topline = &tmp;
                }

                m_printer->print(left.c_str(), left.length());
                m_printer->print(color.c_str(), color.length());
                m_printer->print("\xe2\x94\x8c");                       // ┌
                m_printer->print(topline->c_str(), topline->length());  // ─
                m_printer->print("\xe2\x94\x90\x1b[m");                 // ┐
            }

            // Display items.
            for (int row = 0; row < m_visible_rows; row++)
            {
                int i = m_top + row;
                if (i >= count)
                    break;

                rl_crlf();
                up++;

                move_to_end = true;
                if (m_prev_displayed < 0 ||
                    i == m_index ||
                    i == m_prev_displayed)
                {
                    m_printer->print(left.c_str(), left.length());
                    m_printer->print(color.c_str(), color.length());
                    m_printer->print("\xe2\x94\x82");               // │

                    if (i == m_index)
                        m_printer->print("\x1b[7m");

                    int spaces = col_width - 2;

                    if (m_history_mode)
                    {
                        tmp.clear();
                        tmp.format("%*u: ", max_num_len, i + 1);
                        m_printer->print(tmp.c_str(), tmp.length());
                        spaces -= tmp.length();
                    }

                    int cell_len;
                    int char_len = limit_cells(m_items[i], spaces, cell_len);
                    m_printer->print(m_items[i], char_len);
                    spaces -= cell_len;

                    tmp.clear();
                    while (spaces > 0)
                    {
                        int chunk = min<int>(32, spaces);
                        tmp.concat("                                ", chunk);
                        spaces -= chunk;
                    }

                    m_printer->print(tmp.c_str(), tmp.length());    // text

                    if (i == m_index)
                        m_printer->print("\x1b[27m");

                    m_printer->print("\xe2\x94\x82\x1b[m");         // │
                }
            }

            // Display border.
            if (draw_border)
            {
                rl_crlf();
                up++;
                m_printer->print(left.c_str(), left.length());
                m_printer->print(color.c_str(), color.length());
                m_printer->print("\xe2\x94\x94");                       // └
                m_printer->print(horzline.c_str(), horzline.length());  // ─
                m_printer->print("\xe2\x94\x98\x1b[m");                 // ┘
            }

            m_prev_displayed = m_index;
        }
        else
        {
            // Clear to end of screen.
            m_printer->print("\x1b[m\x1b[J");

            m_prev_displayed = -1;
        }

        // Restore cursor position.
        if (up > 0)
        {
            str<16> s;
            s.format("\x1b[%dA", up);
            m_printer->print(s.c_str(), s.length());
        }
        _rl_move_vert(vpos);
        _rl_last_c_pos = cpos;
        GetConsoleScreenBufferInfo(h, &csbi);
        restore.Y = csbi.dwCursorPosition.Y;
        SetConsoleCursorPosition(h, restore);
    }
}

//------------------------------------------------------------------------------
void textlist_impl::set_top(int top)
{
    assert(top >= 0);
    assert(top <= max<int>(0, m_count - m_visible_rows));
    if (top != m_top)
    {
        m_top = top;
        m_prev_displayed = -1;
    }
}

//------------------------------------------------------------------------------
void textlist_impl::reset()
{
    std::vector<const char*> zap;

    // Don't reset screen row and cols; they stay in sync with the terminal.

    m_visible_rows = 0;
    m_default_title.clear();
    m_override_title.clear();
    m_has_override_title = false;

    m_count = 0;
    m_entries = nullptr;
    m_items = std::move(zap);
    m_longest = 0;

    m_top = 0;
    m_index = 0;
    m_prev_displayed = -1;

    m_needle.clear();
    m_needle_is_number = false;
    m_input_clears_needle = false;

    m_store.clear();
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
popup_results activate_directories_text_list(const char** dirs, int count)
{
    if (!s_textlist)
        return popup_result::error;

    return s_textlist->activate("Directories", dirs, count);
}

//------------------------------------------------------------------------------
popup_results activate_history_text_list(const char** history, int count)
{
    if (!s_textlist)
        return popup_result::error;

    return s_textlist->activate("History", history, count, true/*history_mode*/);
}
