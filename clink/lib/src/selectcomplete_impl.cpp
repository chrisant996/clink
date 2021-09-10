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
#include <terminal/ecma48_iter.h>

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
extern matches* maybe_regenerate_matches(const char* needle, bool popup, bool sort=false);
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
    bind_id_selectcomplete_quote,
    bind_id_selectcomplete_escape,

    bind_id_selectcomplete_catchall,
};

//------------------------------------------------------------------------------
enum {
    between_cols = 2,
    before_desc = 4,

    ellipsis_len = 3,
};

static_assert(between_cols <= before_desc, "description separator can't be less than the column separator");



//------------------------------------------------------------------------------
// Parse ANSI escape codes to determine the visible character length of the
// string (which gets used for column alignment).  Truncate the string with an
// ellipsis if it exceeds a maximum visible length.
void ellipsify(const char* in, int limit, str_base& out, bool expand_ctrl)
{
    int visible_len = 0;
    int truncate_visible = -1;
    int truncate_bytes = -1;

    out.clear();

    ecma48_state state;
    ecma48_iter iter(in, state);
    while (visible_len <= limit)
    {
        const ecma48_code& code = iter.next();
        if (!code)
            break;
        if (code.get_type() == ecma48_code::type_chars)
        {
            const char* prev = code.get_pointer();
            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (const int c = inner_iter.next())
            {
                const int clen = (expand_ctrl && (CTRL_CHAR(c) || c == RUBOUT)) ? 2 : clink_wcwidth(c);
                if (truncate_visible < 0 && visible_len + clen > limit - ellipsis_len)
                {
                    truncate_visible = visible_len;
                    truncate_bytes = out.length();
                }
                if (visible_len + clen > limit)
                {
                    out.truncate(truncate_bytes);
                    out.concat("...", min<int>(ellipsis_len, max<int>(0, limit - truncate_visible)));
                    return;
                }
                visible_len += clen;
                out.concat(prev, inner_iter.get_pointer() - prev);
                prev = inner_iter.get_pointer();
            }
        }
        else
        {
            out.concat(code.get_pointer(), code.get_length());
        }
    }
}



//------------------------------------------------------------------------------
match_adapter::~match_adapter()
{
    free_filtered();
}

//------------------------------------------------------------------------------
const matches* match_adapter::get_matches() const
{
    return m_matches;
}

//------------------------------------------------------------------------------
void match_adapter::set_matches(const matches* matches)
{
    free_filtered();
    m_real_matches = matches;
    m_matches = m_real_matches;
}

//------------------------------------------------------------------------------
void match_adapter::set_regen_matches(const matches* matches)
{
    m_matches = matches ? matches : m_real_matches;
}

//------------------------------------------------------------------------------
void match_adapter::set_filtered_matches(match_display_filter_entry** filtered_matches)
{
    free_filtered();

    m_filtered_matches = filtered_matches;

    // Skip first filtered match; it's fake, to satisfy Readline's expectation
    // that matches start at [1].
    unsigned int count = 0;
    bool has_desc = false;
    if (filtered_matches && filtered_matches[0])
    {
        has_desc = (filtered_matches[0]->visible_display < 0);
        while (*(++filtered_matches))
            count++;
    }
    m_filtered_count = count;
    m_has_descriptions = has_desc;
}

//------------------------------------------------------------------------------
matches_iter match_adapter::get_iter()
{
    assert(m_matches);
    free_filtered();
    return m_matches->get_iter();
}

//------------------------------------------------------------------------------
unsigned int match_adapter::get_match_count() const
{
    if (m_filtered_matches)
        return m_filtered_count;
    if (m_matches)
        return m_matches->get_match_count();
    return 0;
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->match;
    if (m_matches)
        return m_matches->get_match(index);
    return nullptr;
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match_display(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->display;
    if (m_matches)
        return m_matches->get_match(index);
    return nullptr;
}

//------------------------------------------------------------------------------
unsigned int match_adapter::get_match_visible_display(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->visible_display;
    if (m_matches)
    {
        const char* display = printable_part(const_cast<char*>(m_matches->get_match(index)));
        return printable_len(display);
    }
    return 0;
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match_description(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->description;
    return nullptr;
}

//------------------------------------------------------------------------------
unsigned int match_adapter::get_match_visible_description(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->visible_description;
    return 0;
}

//------------------------------------------------------------------------------
match_type match_adapter::get_match_type(unsigned int index) const
{
    if (m_filtered_matches)
        return static_cast<match_type>(m_filtered_matches[index + 1]->type);
    if (m_matches)
        return m_matches->get_match_type(index);
    return match_type::none;
}

//------------------------------------------------------------------------------
bool match_adapter::is_custom_display(unsigned int index) const
{
    if (m_filtered_matches)
    {
        if (!m_filtered_matches[index + 1]->match[0])
            return true;
        const char* temp = printable_part(const_cast<char*>(m_filtered_matches[index + 1]->match));
        if (strcmp(temp, m_filtered_matches[index + 1]->display) != 0)
            return true;
    }
    return false;
}

//------------------------------------------------------------------------------
void match_adapter::free_filtered()
{
    free_filtered_matches(m_filtered_matches);
    m_filtered_matches = nullptr;
    m_filtered_count = 0;
    m_has_descriptions = false;
}



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

    if (reactivate && m_point >= 0 && m_len >= 0 && m_point + m_len <= m_buffer->get_length() && m_inserted)
    {
#ifdef DEBUG
        rollback<int> rb(m_prev_bind_group, 999999); // Dummy to make assertion happy in insert_needle().
#endif
        insert_needle();
    }

    m_inserted = false;
    m_quoted = false;

    m_anchor = -1;
    m_delimiter = 0;
    reset_generate_matches();

    update_matches(true/*restrict*/, true/*sort*/);
    assert(m_anchor >= 0);
    if (m_anchor < 0)
        return false;

    if (!m_matches.get_match_count())
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
        (rl_completion_query_items > 0 && m_matches.get_match_count() >= rl_completion_query_items))
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
        bool yes = get_y_or_n(0) > 0;

        // Erase prompt.
        _rl_move_vert(_rl_vis_botlin);
        rl_crlf();
        m_printer->print("\x1b[K");
        SetConsoleCursorPosition(h, restore);

        if (!yes)
            goto cant_activate;
    }

    // Activate key bindings.
    assert(m_prev_bind_group < 0);
    m_prev_bind_group = result.set_bind_group(m_bind_group);
    m_was_backspace = false;

    // Insert first match.
    bool only_one = (m_matches.get_match_count() == 1);
    m_point = m_buffer->get_cursor();
    m_top = 0;
    m_index = 0;
    m_prev_displayed = -1;
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
    binder.bind(m_bind_group, "^h", bind_id_selectcomplete_backspace);
    binder.bind(m_bind_group, "\\e[3~", bind_id_selectcomplete_delete);
    binder.bind(m_bind_group, " ", bind_id_selectcomplete_space);
    binder.bind(m_bind_group, "\\r", bind_id_selectcomplete_enter);
    binder.bind(m_bind_group, "/", bind_id_selectcomplete_slash);
    binder.bind(m_bind_group, "\\", bind_id_selectcomplete_backslash);
    binder.bind(m_bind_group, "\"", bind_id_selectcomplete_quote);

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
    m_matches.set_matches(&context.matches);
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
    m_matches.set_matches(nullptr);
    m_printer = nullptr;
    m_anchor = -1;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::on_input(const input& _input, result& result, const context& context)
{
    assert(is_active());

    bool sort = false;
    input input = _input;

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
    int count = m_matches.get_match_count();
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

    switch (input.id)
    {
    case bind_id_selectcomplete_next:
next:
        m_index++;
        if (m_index >= count)
            m_index = _rl_menu_complete_wraparound ? 0 : count - 1;
navigated:
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
        {
            const int y = get_match_row(m_index);
            const int rows = min<int>(m_match_rows, m_visible_rows);
            if (input.id == bind_id_selectcomplete_pgup)
            {
                if (!y)
                {
                    m_index = 0;
                }
                else
                {
                    int new_y = max<int>(0, (y == m_top) ? y - (rows - 1) : m_top);
                    m_index += (new_y - y);
                }
                goto navigated;
            }
            else if (input.id == bind_id_selectcomplete_pgdn)
            {
                if (y == m_match_rows - 1)
                {
                    m_index = m_matches.get_match_count() - 1;
                }
                else
                {
                    int new_y = min<int>(m_match_rows - 1, (y == m_top + rows - 1) ? y + (rows - 1) : m_top + (rows - 1));
                    m_index += (new_y - y);
                }
                if (m_index > m_matches.get_match_count() - 1)
                {
                    m_top = max<int>(0, m_match_rows - m_visible_rows);
                    m_index = m_matches.get_match_count() - 1;
                }
                goto navigated;
            }
        }
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
        m_inserted = false; // A subsequent activation should not resume.
        break;

    case bind_id_selectcomplete_space:
        insert_match(2/*final*/);
        cancel(result);
        m_inserted = false; // A subsequent activation should not resume.
        break;

    case bind_id_selectcomplete_enter:
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

    case bind_id_selectcomplete_escape:
        goto revert;

    case bind_id_selectcomplete_catchall:
        {
            // Figure out whether the input is text to be inserted.
            {
                str_iter iter(input.keys, input.len);
                while (iter.more())
                {
                    unsigned int c = iter.next();
                    if (c < ' ' || c == 0x7f)
                    {
                        cancel(result);
                        result.pass();
                        return;
                    }
                }
            }

            // Insert the text.
append_to_needle:
            m_needle.concat(input.keys, input.len);
update_needle:
            m_top = 0;
            m_index = 0;
            m_prev_displayed = -1;
            insert_needle();
            update_matches(false/*restrict*/, sort);
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
    m_top = 0;
    m_index = 0;
    m_prev_displayed = -1;
    m_anchor = line.get_end_word_offset();

    // Update the needle regardless whether active.  This is so update_matches()
    // can filter the filtered matches based on the initial needle.  Because the
    // matches were initially expanded with "g" matching ".git" and "getopt\"
    // but only an explicit wildcard (e.g. "*g") should accept ".git".
    m_needle = needle;
    update_len();
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
    m_matches.set_regen_matches(nullptr);

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
        // Update Readline modes based on the available completions.
        {
            matches_iter iter = m_matches.get_iter();
            while (iter.next())
                ;
            update_rl_modes_from_matches(m_matches.get_matches(), iter, m_matches.get_match_count());
        }
    }

    // Perform match display filtering.
    const bool popup = false;
    bool filtered = false;
    if (m_matches.get_matches()->match_display_filter(nullptr, nullptr, popup))
    {
        assert(rl_completion_matches_include_type);
        if (matches* regen = maybe_regenerate_matches(m_needle.c_str(), popup))
        {
            m_matches.set_regen_matches(regen);

            // Build char** array for filtering.
            std::vector<autoptr<char>> matches;
            const unsigned int count = m_matches.get_match_count();
            matches.emplace_back(nullptr); // Placeholder for lcd.
            for (unsigned int i = 0; i < count; i++)
            {
                const char* text = m_matches.get_match(i);
                const size_t len = strlen(text);
                char* match = static_cast<char*>(malloc(1 + len + 1));
                match[0] = static_cast<char>(m_matches.get_match_type(i));
                memcpy(match + 1, text, len + 1);
                matches.emplace_back(match);
            }
            matches.emplace_back(nullptr);

            // Get filtered matches.
            match_display_filter_entry** filtered_matches = nullptr;
            m_matches.get_matches()->match_display_filter(&*matches.begin(), &filtered_matches, popup);

            // Filter the, uh, filtered matches.
            if (filtered_matches)
            {
                // Need to use printable_part() and etc, but types are separate
                // from matches here.
                rollback<int> rb(rl_completion_matches_include_type, 0);

                const char* needle = printable_part(const_cast<char*>(m_needle.c_str()));
                int needle_len = strlen(needle);

                match_display_filter_entry** tortoise = filtered_matches + 1;
                for (match_display_filter_entry** hare = tortoise; *hare; ++hare)
                {
                    // Discard empty matches.
                    if (!(*hare)->match[0])
                    {
                        free(*hare);
                        continue;
                    }

                    // Discard matches that don't match the needle.
                    int cmp = str_compare(needle, (*hare)->match);
                    if (cmp < 0) cmp = needle_len;
                    if (cmp < needle_len)
                    {
                        free(*hare);
                        continue;
                    }

                    // Keep the match.
                    if (hare != tortoise)
                        *tortoise = *hare;
                    tortoise++;
                }
                *tortoise = nullptr;
            }

            // Use filtered matches.
            m_matches.set_filtered_matches(filtered_matches);
            filtered = true;

#ifdef DEBUG
            if (dbg_get_env_int("DEBUG_FILTER"))
            {
                puts("-- SELECTCOMPLETE MATCH_DISPLAY_FILTER");
                if (filtered_matches && filtered_matches[0])
                {
                    // Skip [0]; Readline's expects matches start at [1].
                    str<> tmp;
                    while (*(++filtered_matches))
                    {
                        match_type_to_string(static_cast<match_type>(filtered_matches[0]->type), tmp);
                        printf("type '%s', match '%s', display '%s'\n",
                                tmp.c_str(),
                                filtered_matches[0]->match,
                                filtered_matches[0]->display);
                    }
                }
                puts("-- DONE");
            }
#endif
        }
    }

    // Determine the lcd.
    if (restrict)
    {
        const unsigned int count = m_matches.get_match_count();
        for (unsigned int i = 0; i < count; i++)
        {
            const char* match = m_matches.get_match(i);
            if (!i)
            {
                m_needle = match;
            }
            else
            {
                int matching = str_compare(m_needle.c_str(), match);
                m_needle.truncate(matching);
            }
        }

        m_lcd = m_needle.length();
    }

    // Determine the longest match.
    if (restrict || filtered)
    {
        // Using m_matches directly means match types are separate from matches.
        // When not filtered, get_match_visible_display() has to synthesize the
        // visible length by using functions that are influenced by
        // rl_completion_matches_include_type.
        rollback<int> rb(rl_completion_matches_include_type, 0);

        if (restrict)
            m_match_longest = 0;

        const unsigned int count = m_matches.get_match_count();
        for (unsigned int i = 0; i < count; i++)
        {
            int len = m_matches.get_match_visible_display(i);
            if (m_match_longest < len)
                m_match_longest = len;
        }
    }

    update_layout();
    update_display();
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_len()
{
    m_len = 0;

    if (m_index < m_matches.get_match_count())
    {
        size_t len = strlen(m_matches.get_match(m_index));
        if (len > m_needle.length())
            m_len = len - m_needle.length();
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_layout()
{
    int slop_rows = 2;

    int cols_that_fit = m_matches.has_descriptions() ? 1 : m_screen_cols / (m_match_longest + between_cols);
    m_match_cols = max<int>(1, cols_that_fit);
    m_match_rows = (m_matches.get_match_count() + (m_match_cols - 1)) / m_match_cols;

    // +3 for quotes and append character (e.g. space).
    int input_height = (_rl_vis_botlin + 1) + (m_match_longest + 3 + m_screen_cols - 1) / m_screen_cols;
    m_visible_rows = m_screen_rows - input_height - slop_rows;
    if (m_visible_rows < 8)
        m_visible_rows = 0;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_top()
{
    const int y = get_match_row(m_index);
    if (m_top > y)
    {
        m_top = y;
    }
    else
    {
        const int rows = min<int>(m_match_rows, m_visible_rows);
        if (m_top + rows <= y)
            m_top = y - rows + 1;
    }
}

//------------------------------------------------------------------------------
void selectcomplete_impl::update_display()
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

        // Using m_matches directly means match types are separate from matches.
        rollback<int> rb(rl_completion_matches_include_type, 0);

        // Move cursor after the input line.
        _rl_move_vert(_rl_vis_botlin);

#ifdef SHOW_DISPLAY_GENERATION
        static char s_chGen = '0';
#endif

        // Display matches.
        int up = 0;
        bool move_to_end = true;
        const int count = m_matches.get_match_count();
        if (is_active() && count > 0)
        {
            update_top();
m_prev_displayed = -1;

            const int rows = min<int>(m_match_rows, m_visible_rows);
            const int major_stride = _rl_print_completions_horizontally ? m_match_cols : 1;
            const int minor_stride = _rl_print_completions_horizontally ? 1 : m_match_rows;
            const int col_width = min<int>(m_match_longest, max<int>(m_screen_cols - between_cols, 1));
            for (int row = 0; row < rows; row++)
            {
                int i = (m_top + row) * major_stride;
                if (i >= count)
                    break;

                rl_crlf();
                up++;

                move_to_end = true;
                if (m_prev_displayed < 0 ||
                    row == get_match_row(m_index) ||
                    row == get_match_row(m_prev_displayed))
                {
                    // Print matches on the row.
                    str<> truncated;
                    str<> tmp;
                    reset_tmpbuf();
#ifdef SHOW_DISPLAY_GENERATION
                    append_tmpbuf_char(s_chGen);
#endif
                    for (int col = 0; col < m_match_cols; col++)
                    {
                        if (i >= count)
                            break;

                        const int selected = (i == m_index);
                        const char* const display = m_matches.get_match_display(i);
                        char* temp = m_matches.is_display_filtered() ? const_cast<char*>(display) : printable_part(const_cast<char*>(display));
                        const match_type match_type = m_matches.get_match_type(i);
                        const unsigned char type = static_cast<unsigned char>(match_type);

                        mark_tmpbuf();
                        int printed_len;
                        if (m_matches.is_display_filtered() &&
                            (is_match_type(match_type, match_type::none) ||
                             m_matches.is_custom_display(i)))
                        {
                            printed_len = m_matches.get_match_visible_display(i);
                            if (printed_len > col_width)
                            {
                                ellipsify(temp, col_width, truncated, false/*expand_ctrl*/);
                                temp = truncated.data();
                                printed_len = cell_count(temp);
                            }
                            if (selected)
                            {
                                ecma48_processor(temp, &tmp, nullptr, ecma48_processor_flags::plaintext);
                                temp = tmp.data();
                            }
                            append_display(temp, selected);
                        }
                        else
                        {
                            printed_len = append_filename(temp, display, 0, type, selected);
                            if (printed_len > col_width)
                            {
                                rollback_tmpbuf();
                                ellipsify(temp, col_width, truncated, true/*expand_ctrl*/);
                                temp = truncated.data();
                                printed_len = append_filename(temp, display, 0, type, selected);
                            }
                        }

                        const int next = i + minor_stride;

                        const char* desc = m_matches.get_match_description(i);
                        const bool last_col = (col + 1 >= m_match_cols || next >= count);
                        if (selected || !last_col || desc)
                            pad_filename(printed_len, col_width + (selected ? 0 : between_cols), selected);

                        if (desc)
                        {
                            // Leave between_cols at end of line, otherwise "\x1b[K" can erase part
                            // of the intended output.
                            const int remaining = m_screen_cols - col_width - before_desc - between_cols;
                            if (remaining > 0)
                            {
                                printed_len = m_matches.get_match_visible_description(i);
                                if (printed_len > remaining)
                                {
                                    ellipsify(desc, remaining, truncated, false/*expand_ctrl*/);
                                    desc = truncated.data();
                                    printed_len = cell_count(desc);
                                }
                                pad_filename(0, before_desc - (selected ? 0 : between_cols), 0);
                                append_tmpbuf_string(desc, -1);
                            }
                        }

                        if (selected && !last_col)
                            pad_filename(0, between_cols, 0);

                        i = next;
                    }
                    flush_tmpbuf();

                    // Clear to end of line.
                    m_printer->print("\x1b[m\x1b[K");
                    move_to_end = false;
                }
            }

            m_prev_displayed = m_index;
        }
        else
        {
            assert(move_to_end);
            m_prev_displayed = -1;
        }

#ifdef SHOW_DISPLAY_GENERATION
        s_chGen++;
        if (s_chGen > 'Z')
            s_chGen = '0';
#endif

        // Move cursor to end of last input line.
        if (move_to_end)
        {
            str<16> s;
            s.format("\x1b[%dG", m_screen_cols + 1);
            m_printer->print(s.c_str(), s.length());
        }

        // Clear to end of screen.
        m_printer->print("\x1b[m\x1b[J");

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
    m_point = m_buffer->get_cursor();
    m_buffer->insert(qs);
    m_buffer->set_cursor(m_point);
    m_buffer->end_undo_group();
    m_inserted = true;
}

//------------------------------------------------------------------------------
void selectcomplete_impl::insert_match(int final)
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

    bool removed_dir_mark = false;
    if (is_match_type(type, match_type::dir) && !_rl_complete_mark_directories)
    {
        int cursor = m_buffer->get_cursor();
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

    if (final)
    {
        int nontrivial_lcd = compare_match(const_cast<char*>(m_needle.c_str()), match);
        str<> match_with_type;
        match_with_type.concat(" ");
        match_with_type.concat(match);
        *match_with_type.data() = static_cast<unsigned char>(type);

        rollback<int> rb(rl_completion_matches_include_type, true);
        bool append_space = false;
        // UGLY: append_to_match() circumvents the m_buffer abstraction.
        append_to_match(match_with_type.data(), m_anchor + !!*qs, m_delimiter, *qs, nontrivial_lcd);
        m_point = m_buffer->get_cursor();

        bool have_space = (m_buffer->get_buffer()[m_point - 1] == ' ');
        assert(!have_space || !*qs); // Quote should not occur after a space.
        m_buffer->insert(qs);

        // Pressing Space to insert a final match needs to maybe add a quote,
        // and then maybe add a space, depending on what append_to_match did.
        if (final == 2 || !is_match_type(type, match_type::dir))
        {
            // A space may or may not be present.  Delete it if one is.
            bool append_space = (final == 2);
            int cursor = m_buffer->get_cursor();
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
        m_point = m_anchor + strlen(qs) + m_needle.length();
    }

    m_buffer->set_cursor(m_point);
    m_buffer->end_undo_group();

    update_len();
    m_inserted = true;

    const int botlin = _rl_vis_botlin;
    m_buffer->draw();
    if (botlin != _rl_vis_botlin)
    {
        // Coax the cursor to the end of the input line.
        const int cursor = m_buffer->get_cursor();
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
        update_layout();
    }
}

//------------------------------------------------------------------------------
int selectcomplete_impl::get_match_row(int index) const
{
    return _rl_print_completions_horizontally ? (index - m_top * m_match_cols) : (index % m_match_rows);
}

//------------------------------------------------------------------------------
bool selectcomplete_impl::is_active() const
{
    return m_prev_bind_group >= 0 && m_buffer && m_printer && m_anchor >= 0 && m_point >= m_anchor;
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
