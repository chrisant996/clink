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

#include <core/base.h>
#include <core/settings.h>
#include <core/str_compare.h>
#include <core/str_iter.h>
#include <rl/rl_commands.h>
#include <terminal/printer.h>

extern "C" {
#include <compat/config.h>
#include <compat/display_matches.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
#include <readline/colors.h>
int compare_match(char* text, const char* match);
int append_to_match(char* text, int orig_start, int delimiter, int quote_char, int nontrivial_match);
char* printable_part(char* text);
void set_completion_defaults(int what_to_do);
int get_y_or_n(int for_pager);
extern int _rl_last_v_pos;
};

extern void reset_generate_matches();
extern void force_update_internal(bool restrict=false, bool sort=false);
extern void update_matches();
extern void update_rl_modes_from_matches(const matches* matches, const matches_iter& iter, int count);



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
    bind_id_selectcomplete_backspace,
    bind_id_selectcomplete_delete,
    bind_id_selectcomplete_space,
    bind_id_selectcomplete_enter,
    bind_id_selectcomplete_slash,
    bind_id_selectcomplete_backslash,
    bind_id_selectcomplete_escape,

    bind_id_selectcomplete_catchall,
};

//------------------------------------------------------------------------------
enum {
    between_cols = 2,
};



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
    assert(m_matches);
    if (!m_buffer || !m_matches)
        return false;

    if (reactivate && m_point >= 0 && m_len >= 0 && m_point + m_len <= m_buffer->get_length() && m_inserted)
    {
#ifdef DEBUG
        rollback<int> rb(m_prev_bind_group, 999999); // Dummy to make assertion happy in insert_needle().
#endif
        insert_needle();
    }
    else
    {
        m_inserted = false;
        m_quoted = false;
    }

    m_anchor = -1;
    m_delimiter = 0;
    reset_generate_matches();

TODO("SELECT-COMPLETE -- if there is a display filter, give it a chance to modify MATCHES (rl_match_display_filter_func)");
    update_matches(true/*restrict*/, true/*sort*/);
    assert(m_anchor >= 0);
    if (m_anchor < 0)
        return false;

    if (!m_matches->get_match_count())
    {
cant_activate:
        m_anchor = -1;
        return false;
    }

    // Make sure there's room.
    update_layout();
    if (m_visible_rows <= 0)
        goto cant_activate;

    // Prompt if too many.
    if (rl_completion_auto_query_items ?
        (m_match_rows > m_visible_rows) :
        (rl_completion_query_items > 0 && m_matches->get_match_count() >= rl_completion_query_items))
    {
        // I gave up trying to coax Readline into righting the cursor position
        // purely using only ANSI codes.
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(h, &csbi);
        COORD restore = csbi.dwCursorPosition;

        // Move cursor after the input line.
        int vpos = _rl_last_v_pos;
        _rl_move_vert(_rl_vis_botlin);
        rl_crlf();

        // Show prompt.
        if (_rl_pager_color)
            _rl_print_pager_color();
        str<> prompt;
        prompt.format("Display all %d possibilities? (y or n) _", m_matches->get_match_count());
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
        bool yes = get_y_or_n(0) > 0;

        // Erase prompt.
        m_printer->print("\x1b[s");
        _rl_move_vert(_rl_vis_botlin);
        rl_crlf();
        m_printer->print("\x1b[K");
        m_printer->print("\x1b[u");

        if (!yes)
            goto cant_activate;
    }

    // Activate key bindings.
    assert(m_prev_bind_group < 0);
    m_prev_bind_group = result.set_bind_group(m_bind_group);
    m_was_backspace = false;

    // Insert first match.
    bool only_one = (m_matches->get_match_count() == 1);
    m_point = m_buffer->get_cursor();
    m_top = 0;
    m_index = 0;
    insert_match(only_one/*final*/);

    // If there's only one match, then we're done.
    if (only_one)
        cancel(result);
    else
        update_display();

    return true;
}

//------------------------------------------------------------------------------
bool selectcomplete_impl::point_within(int in) const
{
    return is_active() && m_point >= 0 && in >= m_point && in < m_point + m_len;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::bind_input(binder& binder)
{
    m_bind_group = binder.create_group("selectcomplete");
    binder.bind(m_bind_group, "\\t", bind_id_selectcomplete_next);
    binder.bind(m_bind_group, "\\e[Z", bind_id_selectcomplete_prev);
    binder.bind(m_bind_group, "\\e[A", bind_id_selectcomplete_up);
    binder.bind(m_bind_group, "\\e[B", bind_id_selectcomplete_down);
    binder.bind(m_bind_group, "\\e[D", bind_id_selectcomplete_left);
    binder.bind(m_bind_group, "\\e[C", bind_id_selectcomplete_right);
    binder.bind(m_bind_group, "\\e[5~", bind_id_selectcomplete_pgup);
    binder.bind(m_bind_group, "\\e[6~", bind_id_selectcomplete_pgdn);
    binder.bind(m_bind_group, "^h", bind_id_selectcomplete_backspace);
    binder.bind(m_bind_group, "\\e[3~", bind_id_selectcomplete_delete);
    binder.bind(m_bind_group, " ", bind_id_selectcomplete_space);
    binder.bind(m_bind_group, "\\r", bind_id_selectcomplete_enter);
    binder.bind(m_bind_group, "/", bind_id_selectcomplete_slash);
    binder.bind(m_bind_group, "\\", bind_id_selectcomplete_backslash);
    binder.bind(m_bind_group, get_bindable_esc(), bind_id_selectcomplete_escape);

    binder.bind(m_bind_group, "", bind_id_selectcomplete_catchall);
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_begin_line(const context& context)
{
    assert(!s_selectcomplete);
    s_selectcomplete = this;
    m_buffer = &context.buffer;
    m_matches = &context.matches;
    m_printer = &context.printer;
    m_anchor = -1;

    m_screen_cols = context.printer.get_columns();
    m_screen_rows = context.printer.get_rows();
    update_layout();
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_end_line()
{
    s_selectcomplete = nullptr;
    m_buffer = nullptr;
    m_matches = nullptr;
    m_printer = nullptr;
    m_anchor = -1;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_input(const input& input, result& result, const context& context)
{
    assert(is_active());

    bool sort = false;

    // Convert double Backspace into Escape.
    if (input.id != bind_id_selectcomplete_backspace)
        m_was_backspace = false;
    else if (m_was_backspace)
        goto revert;

    // Cancel if no matches (which shouldn't be able to happen here).
    int count = m_matches->get_match_count();
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
        result.pass();
        return;
    }

TODO("SELECT-COMPLETE -- handle top of viewable sublist");
    switch (input.id)
    {
    case bind_id_selectcomplete_next:
next:
        m_index++;
        if (m_index >= count)
            m_index = _rl_menu_complete_wraparound ? 0 : count - 1;
navigated:
TODO("SELECT-COMPLETE -- top");
        insert_match();
        update_display();
        break;
    case bind_id_selectcomplete_prev:
prev:
        m_index--;
        if (m_index < 0)
            m_index = _rl_menu_complete_wraparound ? count - 1 : 0;
        goto navigated;

    case bind_id_selectcomplete_up:
        if (_rl_print_completions_horizontally)
        {
            int c = m_index % m_match_cols;
            m_index -= m_match_cols;
            if (m_index < 0)
            {
                m_index += (m_match_cols * m_match_rows);
                if (m_index >= count)
                    m_index = count - 1;
            }
            goto navigated;
        }
        goto prev;
    case bind_id_selectcomplete_down:
        if (_rl_print_completions_horizontally)
        {
            int c = m_index % m_match_cols;
            m_index += m_match_cols;
            if (m_index >= count)
            {
                m_index -= (m_match_cols * m_match_rows);
                assert(m_index >= 0);
                if (m_index < 0)
                    m_index = 0;
            }
            goto navigated;
        }
        goto next;

    case bind_id_selectcomplete_left:
        if (!_rl_print_completions_horizontally)
        {
            m_index -= m_match_rows;
            if (m_index < 0)
                m_index = 0;
            goto navigated;
        }
        goto prev;
    case bind_id_selectcomplete_right:
        if (!_rl_print_completions_horizontally)
        {
            m_index += m_match_rows;
            if (m_index >= count)
                m_index = count - 1;
            goto navigated;
        }
        goto next;

    case bind_id_selectcomplete_pgup:
    case bind_id_selectcomplete_pgdn:
        break;

    case bind_id_selectcomplete_backspace:
        if (m_needle.length() <= m_lcd)
        {
            m_was_backspace = true;
        }
        else if (m_needle.length())
        {
            int point = _rl_find_prev_mbchar(const_cast<char*>(m_needle.c_str()), m_needle.length(), MB_FIND_NONZERO);
            m_needle.truncate(point);
            sort = true;
            goto update_needle;
        }
        break;

    case bind_id_selectcomplete_delete:
delete_completion:
        insert_needle();
        cancel(result);
        break;

    case bind_id_selectcomplete_space:
pass_through_at_end:
        m_buffer->set_cursor(m_point + m_len + m_quoted); // Past quotes, if any.
        cancel(result);
        result.pass();
        break;

    case bind_id_selectcomplete_enter:
        insert_match(true/*final*/);
        cancel(result);
        break;

    case bind_id_selectcomplete_slash:
        if (is_match_type(m_matches->get_match_type(m_index), match_type::dir))
            goto pass_through_at_end;
        m_needle.concat("/");
        goto delete_completion;
    case bind_id_selectcomplete_backslash:
        if (is_match_type(m_matches->get_match_type(m_index), match_type::dir))
        {
            m_buffer->set_cursor(m_point + m_len); // Inside quotes, if any.
            cancel(result);
            break;
        }
        m_needle.concat("\\");
        goto delete_completion;

    case bind_id_selectcomplete_escape:
revert:
        if (m_inserted)
        {
            m_buffer->undo();
            m_inserted = false;
        }
        cancel(result);
        break;

    case bind_id_selectcomplete_catchall:
        {
            bool selfinsert = true;

            {
                str_iter iter(input.keys, input.len);
                while (iter.more())
                {
                    unsigned int c = iter.next();
                    if (c < ' ' || c == 0x7f)
                    {
                        selfinsert = false;
                        break;
                    }
                }
            }

            if (selfinsert)
            {
                m_needle.concat(input.keys, input.len);
update_needle:
                m_top = 0;
                m_index = 0;
                insert_needle();
                update_matches(false/*restrict*/, sort);
                if (m_matches->get_match_count())
                    insert_match();
                else
                    cancel(result);
            }
            else
            {
                cancel(result);
                result.pass();
            }
        }
        break;
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
    m_top = 0;
    m_index = 0;
    m_anchor = line.get_end_word_offset();

    if (is_active())
    {
        m_needle = needle;
        update_len();
        update_layout();
        update_display();
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_terminal_resize(int columns, int rows, const context& context)
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
void selectcomplete_impl::cancel(editor_module::result& result)
{
    assert(is_active());

    // Leave m_point and m_len alone so that activate() can reactivate if
    // necessary.

    m_buffer->set_need_draw();

    result.set_bind_group(m_prev_bind_group);
    m_prev_bind_group = -1;

    reset_generate_matches();

    update_display();
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_matches(bool restrict, bool sort)
{
    ::force_update_internal(restrict, sort);

    // Initialize when starting a new interactive completion.
    if (restrict)
    {
        set_completion_defaults('%');

        int found_quote = 0;
        int quote_char = 0;

        if (m_buffer->get_cursor())
        {
            int tmp = m_buffer->get_cursor();
            quote_char = _rl_find_completion_word(&found_quote, &m_delimiter);
            m_buffer->set_cursor(tmp);
        }

        rl_completion_found_quote = found_quote;
        rl_completion_quote_character = quote_char;
        rl_completion_matches_include_type = 1;
    }

    // Update matches.
    ::update_matches();

    // Find lcd when starting a new interactive completion.
    if (restrict)
    {
        // Using m_matches directly means match types are separate from matches.
        rollback<int> rb(rl_completion_matches_include_type, 0);

        m_match_longest = 0;

        bool first = true;
        matches_iter iter = m_matches->get_iter();
        while (iter.next())
        {
            if (first)
            {
                first = false;
                m_needle = iter.get_match();
            }
            else
            {
                int matching = str_compare(m_needle.c_str(), iter.get_match());
                m_needle.truncate(matching);
            }

            int len = printable_len(iter.get_match());
            if (m_match_longest < len)
                m_match_longest = len;
        }

        m_lcd = m_needle.length();

        update_rl_modes_from_matches(m_matches, iter, m_matches->get_match_count());
    }

TODO("SELECT-COMPLETE -- to handle match filtering it will be necessary to use a copied char** array rather than directly using matches_impl");
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_len()
{
    assert(is_active());

    m_len = 0;

    if (m_index < m_matches->get_match_count() && m_matches)
    {
        size_t len = strlen(m_matches->get_match(m_index));
        if (len > m_needle.length())
            m_len = len - m_needle.length();
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_layout()
{
    int slop_rows = 2;

    int cols_that_fit = m_screen_cols / (m_match_longest + between_cols);
    m_match_cols = max<int>(1, cols_that_fit);
    m_match_rows = (m_matches->get_match_count() + (m_match_cols - 1)) / m_match_cols;

    int input_height = (_rl_vis_botlin + 1) + (m_match_longest + m_screen_cols - 1) / m_screen_cols;
    m_visible_rows = m_screen_rows - input_height - slop_rows;
    if (m_visible_rows < 8)
        m_visible_rows = 0;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_display()
{
    if (m_visible_rows > 0)
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(h, &csbi);
        COORD restore = csbi.dwCursorPosition;

        // Using m_matches directly means match types are separate from matches.
        rollback<int> rb(rl_completion_matches_include_type, 0);

        // Move cursor after the input line.
        const int vpos = _rl_last_v_pos;
        _rl_move_vert(_rl_vis_botlin);

TODO("SELECT-COMPLETE -- when possible, only update old selected and new selected");

        // Display matches.
        int up = 0;
        if (is_active())
        {
            const int count = m_matches->get_match_count();
            const int rows = min<int>(m_match_rows, m_visible_rows);
            const int major_stride = _rl_print_completions_horizontally ? m_match_cols : 1;
            const int minor_stride = _rl_print_completions_horizontally ? 1 : m_match_rows;
            const int col_width = min<int>(m_match_longest, max<int>(m_screen_cols - between_cols, 1));
            for (int row = 0; row < rows; row++)
            {
                int i = row * major_stride;
                if (i >= count)
                    break;

                rl_crlf();
                up++;

                reset_tmpbuf();
                for (int col = 0; col < m_match_cols; col++)
                {
                    if (i >= count)
                        break;

                    const int selected = (i == m_index);
                    const char* const match = m_matches->get_match(i);
                    char* const temp = printable_part(const_cast<char*>(match));
                    const int printed_len = append_filename(temp, match, 0, static_cast<unsigned char>(m_matches->get_match_type(i)), selected);
                    i += minor_stride;

                    if (selected || (col + 1 < m_match_cols && i < count))
                        pad_filename(printed_len, col_width + between_cols, selected);
                }
                flush_tmpbuf();

                // Clear to end of line.
                m_printer->print("\x1b[K");
            }
        }
        else
        {
            // Move cursor to end of last input line.
            str<16> s;
            s.format("\x1b[%dG", m_screen_cols + 1);
            m_printer->print(s.c_str(), s.length());
        }

        // Clear to end of screen.
        m_printer->print("\x1b[J");

        // Restore cursor position.
        if (up > 0)
        {
            str<16> s;
            s.format("\x1b[%dA", up);
            m_printer->print(s.c_str(), s.length());
        }
        _rl_move_vert(vpos);
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

    m_buffer->begin_undo_group();
    m_buffer->remove(m_anchor, m_buffer->get_cursor());
    m_buffer->set_cursor(m_anchor);
    m_buffer->insert(m_needle.c_str());
    m_point = m_buffer->get_cursor();
    m_buffer->end_undo_group();
    m_inserted = true;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::insert_match(bool final)
{
    assert(is_active());

    if (m_inserted)
    {
        m_buffer->undo();
        m_inserted = false;
        m_quoted = false;
    }

    m_len = 0;

    assert(m_index < m_matches->get_match_count());
    const char* match = m_matches->get_match(m_index);

    char qs[2] = {};
    if (match &&
        !rl_completion_found_quote &&
        rl_completer_quote_characters &&
        rl_completer_quote_characters[0] &&
        rl_filename_completion_desired &&
        rl_filename_quoting_desired &&
        rl_filename_quote_characters &&
        _rl_strpbrk(match, rl_filename_quote_characters) != 0)
    {
        qs[0] = rl_completer_quote_characters[0];
        m_quoted = true;
    }

    m_buffer->begin_undo_group();
    m_buffer->remove(m_anchor, m_buffer->get_cursor());
    m_buffer->set_cursor(m_anchor);
    m_buffer->insert(qs);
    m_buffer->insert(match);
    m_buffer->insert(qs);
    m_point = m_anchor + strlen(qs) + m_needle.length();
    if (final)
    {
        int nontrivial_lcd = compare_match(const_cast<char*>(m_needle.c_str()), match);
        append_to_match(const_cast<char*>(match), m_anchor, m_delimiter, *qs, nontrivial_lcd);
    }
    else
    {
        m_buffer->set_cursor(m_point);
    }
    m_buffer->end_undo_group();
    update_len();
    m_inserted = true;
}

//------------------------------------------------------------------------------
bool selectcomplete_impl::is_active() const
{
    return m_prev_bind_group >= 0 && m_buffer && m_matches && m_printer && m_anchor >= 0 && m_point >= m_anchor;
}



//------------------------------------------------------------------------------
bool activate_select_complete(editor_module::result& result, bool reactivate)
{
    if (!s_selectcomplete)
        return false;

    return s_selectcomplete->activate(result, reactivate);
}

//------------------------------------------------------------------------------
bool point_in_select_complete(int in)
{
    if (!s_selectcomplete)
        return false;
    return s_selectcomplete->point_within(in);
}
