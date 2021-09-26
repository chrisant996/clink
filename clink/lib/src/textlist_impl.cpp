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
    bind_id_textlist_backspace,
    bind_id_textlist_enter,
    bind_id_textlist_edit,
    bind_id_textlist_copy,
    bind_id_textlist_escape,

    bind_id_textlist_catchall,
};

//------------------------------------------------------------------------------
extern const char* get_popup_colors(bool* is_reversed=nullptr);

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
bool textlist_impl::activate(editor_module::result& result, textlist_line_getter_t getter, int count)
{
    clear_items();

    assert(m_buffer);
    if (!m_buffer)
        return false;

    if (!getter || count <= 0)
        return false;

    // Make sure there's room.
    update_layout();
    if (m_visible_rows <= 0)
        return false;

    // Gather the items.
    str<> tmp;
    m_getter = getter;
    for (int i = 0; i < count; i++)
    {
        m_longest = max<int>(m_longest, make_item(getter(i), tmp));
        m_items.push_back(m_store.add(tmp.c_str()));
    }

    // Activate key bindings.
    assert(m_prev_bind_group < 0);
    m_prev_bind_group = result.set_bind_group(m_bind_group);

    // Initialize list.
    m_prev_displayed = -1;
    m_needle.clear();
    m_count = count;
    m_index = count - 1;
    m_top = max<int>(0, count - m_visible_rows);

    show_cursor(false);
    lock_cursor(true);

    update_display();

    return true;
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
    binder.bind(m_bind_group, "^h", bind_id_textlist_backspace);
    binder.bind(m_bind_group, "\\r", bind_id_textlist_enter);
    binder.bind(m_bind_group, "^e", bind_id_textlist_edit);
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
    assert(is_active());

    input input = _input;

    // Cancel if no room.
    if (m_visible_rows <= 0)
    {
        cancel(result);
        result.pass();
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

    case bind_id_textlist_backspace:
        if (m_needle.length())
        {
            int point = _rl_find_prev_mbchar(const_cast<char*>(m_needle.c_str()), m_needle.length(), MB_FIND_NONZERO);
            m_needle.truncate(point);
            goto update_needle;
        }
        break;

    case bind_id_textlist_enter:
    case bind_id_textlist_edit:
        m_buffer->begin_undo_group();
        m_buffer->remove(0, m_buffer->get_length());
        m_buffer->set_cursor(0);
        m_buffer->insert(m_getter(m_index));
        m_buffer->end_undo_group();
        cancel(result);
        if (input.id == bind_id_textlist_enter)
        {
            rl_newline(1, 0);
            result.done();
        }
        break;

    case bind_id_textlist_copy:
        {
            const char* text = m_getter(m_index);
            os::set_clipboard_text(text, int(strlen(text)));
        }
        break;

    case bind_id_textlist_escape:
        cancel(result);
        break;

    case bind_id_textlist_catchall:
        {
            // Figure out whether to add the input to the needle.
            bool add = true;
            {
                str_iter iter(input.keys, input.len);
                while (iter.more())
                {
                    unsigned int c = iter.next();
                    if (c < ' ' || c == 0x7f)
                    {
                        add = false;
                        break;
                    }
                }
            }

            // Update the needle.
            if (add)
            {
                m_needle.concat(input.keys, input.len);
update_needle:
                m_top = 0;
                m_index = 0;
                m_prev_displayed = -1;
// TODO: find needle, scroll accordingly.
            }
        }
        break;
    }
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

    if (is_active())
    {
        update_layout();
        update_display();
    }
}

//------------------------------------------------------------------------------
void textlist_impl::cancel(editor_module::result& result)
{
    assert(is_active());

    result.set_bind_group(m_prev_bind_group);
    m_prev_bind_group = -1;

    lock_cursor(false);
    show_cursor(true);

    update_display();

    _rl_move_vert(0);
    m_buffer->redraw();

    clear_items();
}

//------------------------------------------------------------------------------
void textlist_impl::update_layout()
{
    int slop_rows = 2;
    int border_rows = 2;
    int target_rows = min<int>(20, (m_screen_rows / 2) - border_rows - slop_rows);

    m_visible_rows = target_rows;

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
        if (is_active() && count > 0)
        {
            update_top();

            const int longest = max<int>(m_longest, 40);
            const int col_width = min<int>(longest + 2, m_screen_cols - 8);

            str<> tmp;
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

            tmp.format("%u", m_count);
            const int max_num_len = tmp.length();

            bool reversed = false;
            const char* color = get_popup_colors(&reversed);
            int color_len = int(strlen(color));

            // Display border.
            m_printer->print(left.c_str(), left.length());
            m_printer->print(color, color_len);
            m_printer->print("\xe2\x94\x8c");                       // ┌
            m_printer->print(horzline.c_str(), horzline.length());  // ─
            m_printer->print("\xe2\x94\x90\x1b[m");                 // ┐

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
                    m_printer->print(color, color_len);
                    m_printer->print("\xe2\x94\x82");               // │

                    if (i == m_index)
                    {
                        if (reversed)
                            m_printer->print("\x1b[m");
                        else
                            m_printer->print("\x1b[7m");
                    }

                    int spaces = col_width - 2;

                    tmp.clear();
                    tmp.format("%*u: ", max_num_len, i + 1);
                    m_printer->print(tmp.c_str(), tmp.length());
                    spaces -= tmp.length();

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
                    {
                        if (reversed)
                            m_printer->print("\x1b[7");
                        else
                            m_printer->print("\x1b[27m");
                    }

                    m_printer->print("\xe2\x94\x82\x1b[m");         // │
                }
            }

            // Display border.
            rl_crlf();
            up++;
            m_printer->print(left.c_str(), left.length());
            m_printer->print(color, color_len);
            m_printer->print("\xe2\x94\x94");                       // └
            m_printer->print(horzline.c_str(), horzline.length());  // ─
            m_printer->print("\xe2\x94\x98\x1b[m");                 // ┘

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
void textlist_impl::clear_items()
{
    std::vector<const char*> zap;
    m_items = std::move(zap);
    m_store.clear();
    m_longest = 0;
    m_getter = nullptr;
}

//------------------------------------------------------------------------------
bool textlist_impl::is_active() const
{
    return m_prev_bind_group >= 0 && m_buffer && m_printer && m_visible_rows > 0;
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
static const char* get_history_line(int index)
{
    if (index < 0 || index >= history_length)
        return "";

    HIST_ENTRY** list = history_list();
    if (!list)
        return "";

    const char* line = list[index]->line;
    return line ? line : "";
}

//------------------------------------------------------------------------------
bool activate_history_text_list(editor_module::result& result)
{
    if (!s_textlist)
        return false;

    return s_textlist->activate(result, get_history_line, history_length);
}
