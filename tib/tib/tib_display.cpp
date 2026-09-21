// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_buffer.h"
#include "tib_terminal.h"
#include "tib_termcap.h"
#include "tib_display.h"
#include "tib_context.h"
#include "wcwidth.h"
#include <assert.h>

namespace tib {

bool g_coalesce_output = true;
bool g_show_hide_cursor = true;

static bool s_show_statistics = false;

constexpr uint16_t c_right_text_padding = 2;
constexpr textpos_t c_padding_row_offset = int32_max;

const border_definition c_light_border =
{
    "┌",    "─",    "┐",
    "│",            "│",
    "└",    "─",    "┘",

    1,      1,      1,
    1,              1,
    1,      1,      1,
};

// FUTURE: this project does not yet have an ECMA48 compliant parser, so this
// can't verify any of the widths.
#if 0
bool border_definition::is_valid() const
{
    if (has_left() && left_2 && get_width(left, left_width) != cell_count(left_2, -1))
        return false;
    if (has_right() && right_2 && get_width(right, right_width) != cell_count(right_2, -1))
        return false;

    // FUTURE: verify that attested widths are accurate.
    return true;
}
#endif

int8_t border_definition::get_width(const char* s, int8_t width) const
{
    return (!s || !*s) ? 0 : (width < 0) ? __wcswidth(s, -1) : width;
}

void show_display_manager_statistics(bool show)
{
    s_show_statistics = show;
}

struct stat_coll
{
    size_t total = 0;
    size_t action = 0;
    size_t nop = 0;
    size_t row_total = 0;
    size_t row_action = 0;
    size_t row_nop = 0;
    size_t addl_total = 0;
    size_t addl_action = 0;
    size_t addl_nop = 0;
    size_t error = 0;
};

static stat_coll s_build;
static stat_coll s_display;

static void display_statistics()
{
    assert(s_show_statistics);
    assert(s_build.total == s_build.action + s_build.nop + s_build.error);
    assert(s_display.total == s_display.action + s_display.nop + s_display.error);
    assert(s_display.row_total == s_display.row_action + s_display.row_nop);
    assert(s_display.addl_total == s_display.addl_action + s_display.addl_nop);

    cstring s;
    s.append("\x1b[s\x1b[H\x1b[0;48;2;80;0;80;97m\x1b[K");
    s.printf("build:    %u total, %u action, %u nop, %u row total, %u addl total, %u errors",
             s_build.total, s_build.action, s_build.nop, s_build.row_total, s_build.addl_total, s_build.error);
    s.append("\r\n\x1b[K");
    s.printf("display:  %u total, %u action, %u nop, %u errors",
             s_display.total, s_display.action, s_display.nop, s_display.error);
    s.append("\r\n\x1b[K");
    s.printf("   rows:  %u total, %u action, %u nop  /  addl:  %u total, %u action, %u nop",
             s_display.row_total, s_display.row_action, s_display.row_nop,
             s_display.addl_total, s_display.addl_action, s_display.addl_nop);
    s.append("\x1b[m\x1b[u");
    term_out(s.c_str(), s.length());
}

#define DISPLAY_STATISTICS() do { if (s_show_statistics) display_statistics(); } while (false)

#ifdef _WIN32
// When the Windows legacy console window's visible area is a subset of the
// console width, then the visible area can jitter around or can accidentally
// clip the region that gets cleared by CSI K (Erase in Line, aka EL).  The
// technique encapsulated in preserve_window_horiz_scroll_position minimizes
// the amount of jitter.
class preserve_window_horiz_scroll_position
{
public:
                        preserve_window_horiz_scroll_position(HANDLE h, display_manager* mgr);
                        ~preserve_window_horiz_scroll_position();
private:
    static int32_t      s_nested;
    static bool         s_saved_can_use_clreol;
    static HANDLE       s_h;
    static display_manager* s_mgr;
    static CONSOLE_SCREEN_BUFFER_INFO s_window;
};

static bool s_can_use_clreol = true;

int32_t preserve_window_horiz_scroll_position::s_nested = 0;
bool preserve_window_horiz_scroll_position::s_saved_can_use_clreol = true;
HANDLE preserve_window_horiz_scroll_position::s_h = nullptr;
display_manager* preserve_window_horiz_scroll_position::s_mgr = nullptr;
CONSOLE_SCREEN_BUFFER_INFO preserve_window_horiz_scroll_position::s_window;

preserve_window_horiz_scroll_position::preserve_window_horiz_scroll_position(HANDLE h, display_manager* mgr)
{
    assert(implies(!s_nested, !s_h));
    ++s_nested;
    if (!s_h && h)
    {
        s_saved_can_use_clreol = s_can_use_clreol;
        // TODO: have a global setting to always allow clreol (e.g. Clink's
        // internal terminal emulator implementation of CSI K doesn't have the
        // clipping issue).
        s_can_use_clreol = false;

        s_h = h;
        s_mgr = mgr;
        s_mgr->do_flush();
        GetConsoleScreenBufferInfo(s_h, &s_window);
    }
}

preserve_window_horiz_scroll_position::~preserve_window_horiz_scroll_position()
{
    assert(s_nested > 0);
    if (s_h)
    {
        s_mgr->do_flush();
        CONSOLE_SCREEN_BUFFER_INFO cursor;
        GetConsoleScreenBufferInfo(s_h, &cursor);
        if (cursor.srWindow.Right - cursor.srWindow.Left == s_window.srWindow.Right - s_window.srWindow.Left &&
            cursor.srWindow.Bottom - cursor.srWindow.Top == s_window.srWindow.Bottom - s_window.srWindow.Top &&
            cursor.srWindow.Left != s_window.srWindow.Left &&
            cursor.dwCursorPosition.Y >= s_window.srWindow.Top &&
            cursor.dwCursorPosition.Y <= s_window.srWindow.Bottom)
        {
            // Only restore the horizontal scroll position.  If the vertical
            // scroll position is also restored, then this interferes with
            // text output scrolling the terminal vertically when it goes past
            // the bottom of the visible window.
            const SHORT currentLeft = cursor.srWindow.Left;
            SHORT delta = 0;
            cursor.srWindow.Left = s_window.srWindow.Left;
            cursor.srWindow.Right = s_window.srWindow.Right;
            if (cursor.dwCursorPosition.X < cursor.srWindow.Left)
                delta = cursor.dwCursorPosition.X - cursor.srWindow.Left;
            else if (cursor.dwCursorPosition.X > cursor.srWindow.Right)
                delta = cursor.dwCursorPosition.X - cursor.srWindow.Right;
            cursor.srWindow.Left += delta;
            cursor.srWindow.Right += delta;
            if (cursor.srWindow.Left != currentLeft)
                SetConsoleWindowInfo(s_h, true, &cursor.srWindow);
        }
    }
    --s_nested;
    if (!s_nested)
    {
        if (s_h)
            s_can_use_clreol = s_saved_can_use_clreol;
        s_saved_can_use_clreol = true;

        s_h = nullptr;
        s_mgr = nullptr;
    }
}

bool is_autowrap_bug_present()
{
#pragma warning(push)
#pragma warning(disable:4996)
    OSVERSIONINFO ver = {sizeof(ver)};
    if (GetVersionEx(&ver))
        return ver.dwMajorVersion < 10;
    return false;
#pragma warning(pop)
}
#endif // _WIN32

static textpos_t back_up_by_amount(textpos_t pos, const char* s, size_t len, size_t backup)
{
    while (pos > 0 && backup)
    {
        uint16_t width;
        const textpos_t prev = backward_one_grapheme(s, len, pos, &width);
        if (backup < width)
            break;
        pos = prev;
        backup -= width;
    }
    return pos;
}

static int16_t get_horiz_scrolled_width(uint16_t width, uint16_t replaced_width)
{
    if (width < replaced_width)
        return width;
    return width - replaced_width + c_horz_scroll_indicator_chars;
}

void text_and_width::clear()
{
    m_text.clear();
    m_width = 0;
}

bool text_and_width::set(const char* text, uint16_t width)
{
    if (!text && !length())
        return false;
    if (text && length() && m_text == text && m_width == width)
        return false;
    m_text = text;
    m_width = width;
    return true;
}

bool text_and_width::set(cstring&& text, uint16_t width)
{
    if (!text.length() && !length())
        return false;
    if (text.length() && length() && m_text == text && m_width == width)
        return false;
    m_text = std::move(text);
    m_width = width;
    return true;
}

bool text_and_width::operator==(const text_and_width& other) const
{
    if (length() != other.length())
        return false;
    if (m_width != other.m_width)
        return false;
    if (!m_text.equals(other.m_text))
        return false;
    return true;
}

bool additional_display_line::operator==(const additional_display_line& other) const noexcept
{
    return width == other.width && bounded == other.bounded && text == other.text;
}

display_line::display_line(uint16_t x1)
: m_x1(x1)
, m_x2(x1)
{
    assert(m_x1);
}

void display_line::append(const char* p, uint32_t len, uint32_t width, char face)
{
    assert(len != c_auto_length);
    m_text.append(p, len);
    const size_t faces_len = m_faces.length();
    memset(m_faces.reserve(faces_len + len) + faces_len, face, len);
    m_faces.set_length(faces_len + len);

    this->m_x2 += width;
}

void display_line::calculate_multiline_scroll_marker(uint16_t max_width)
{
    assert(!m_trail_scroller_width_displaced);
    assert(!m_trail_scroller_len_displayed);

    while (width() < max_width)
        append(" ", 1, 1, FACE_DEFAULT);

    // NOTE: max width should always be at least 8.
    if (width() > c_horz_scroll_indicator_chars)
    {
        const uint32_t num = c_horz_scroll_indicator_chars;
        uint32_t width_displaced = 0;
        uint32_t len_displaced = 0;
        uint32_t pos = uint32_t(m_text.length());
        while (pos > 0 && width_displaced < num)
        {
            uint16_t wc;
            const uint32_t new_pos = backward_one_grapheme(m_text.c_str(), m_text.length(), pos, &wc);
            width_displaced += wc;
            len_displaced += (pos - new_pos);
            pos = new_pos;
        }

        m_trail_scroller_width_displaced = width_displaced;
        m_trail_scroller_len_displayed = len_displaced;
    }
}

void display_lines::clear()
{
    m_top = 0;
    m_pos = 0;
    m_anchor = 0;
    m_left = 0;
    m_change_counter = 0;

    m_lines.clear();
    m_rows.clear();
    m_left_text.clear();
    m_right_text.clear();
    m_right_text_row = -1;
    m_additional_lines.clear();
    m_cursor = { -1, -1 };

    m_inner_offset = { 0, 0 };
    m_extent = { 0, 0 };
    m_phantom_last_row = false;
}

void display_lines::apply_scroll_markers(int16_t x_extent, int32_t y_extent, int32_t total_rows)
{
    // NOTE:  Horizontal scroll markers work differently and are applied
    // separately.
    assert(!m_lines.empty());
    if (y_extent <= 1 || y_extent >= total_rows)
        return;

    const bool above = (m_top > 0);
    const bool below = (m_top + y_extent < total_rows);

    // Apply scroll marker to last row.
    if (below)
    {
        display_line& d = *m_lines.back();
        if (!d.m_trail_scroller_width_displaced && !d.m_trail_scroller_len_displayed)
            d.calculate_multiline_scroll_marker(x_extent);

        // NOTE: max width should always be at least 8.
        assert(d.width() > c_horz_scroll_indicator_chars);
        // if (d.width() > 2)
        if (d.m_trail_scroller_width_displaced && d.m_trail_scroller_len_displayed)
        {
            const uint32_t num = c_horz_scroll_indicator_chars;
            uint32_t width_displaced = d.m_trail_scroller_width_displaced;
            const uint32_t len_displaced = d.m_trail_scroller_len_displayed;

            d.m_text.set_length(d.m_text.length() - len_displaced);
            d.m_faces.set_length(d.m_faces.length() - len_displaced);
            d.m_x2 -= width_displaced;

            for (uint32_t i = num; i--;)
            {
                d.append(">", 1, 1, FACE_SCROLLER);
                --width_displaced;
            }
            while (width_displaced--)
                d.append(" ", 1, 1, FACE_DEFAULT);
        }
    }

    // Apply scroll marker to first row.
    if (above)
    {
        display_line& d = *m_lines[0].get();

        if (!d.m_text.length())
        {
            for (uint32_t num = c_horz_scroll_indicator_chars; num--;)
                d.append("<", 1, 1, FACE_SCROLLER);
        }
        else
        {
            const uint32_t num = c_horz_scroll_indicator_chars;
            uint32_t width_displaced = 0;
            uint32_t len_displaced = 0;
            wcwidth_iter iter_top(d.m_text.c_str(), d.m_text.length());
            while (iter_top.next())
            {
                auto wc = iter_top.character_wcwidth_onectrl();
                auto bytes = iter_top.character_length();

                width_displaced += wc;
                len_displaced += bytes;

                if (width_displaced >= num && len_displaced >= num)
                    break;
            }

            // d.m_lead_scroller_width = c_horz_scroll_indicator_chars;

            for (uint32_t i = 0; i < num && i < d.m_text.length(); ++i)
            {
                assert(width_displaced);
                assert(len_displaced);

                d.m_text.set_at(i, '<');
                d.m_faces.set_at(i, FACE_SCROLLER);
                --width_displaced;
                --len_displaced;
            }

            if (len_displaced > 0)
            {
                d.m_text.delete_range(num, len_displaced);
                d.m_faces.delete_range(num, len_displaced);
            }
            while (width_displaced-- > 0)
                d.append(" ", 1, 1, FACE_DEFAULT);
        }
    }
}

display_manager::display_manager()
#ifdef _WIN32
: m_autowrap_bug(is_autowrap_bug_present())
#endif
{
    m_term_size = get_terminal_size();
}

void display_manager::init_layout(const layout_info* layout)
{
    m_layout = layout;
    m_displayed.clear();
    m_top = 0;
    invalidate();
    invalidate_border();
}

void display_manager::init_buffer(const input_buffer* buffer)
{
    m_buffer = buffer;
    m_top = 0;
    invalidate();
}

void display_manager::init_style(const style_info* style)
{
    m_style = style;
    force_redisplay();
    invalidate_border();
}

void display_manager::init_faces(const face_definitions* face_defs)
{
    m_face_defs = face_defs;
    force_redisplay();
}

void display_manager::init_callbacks(editor_callbacks* callbacks)
{
    m_callbacks = callbacks;
    invalidate();
}

void display_manager::set_origin(int32_t x, int32_t y)
{
    assert(x != 0);
    assert(y != 0);
    m_origin.x = (x == uint32_t(-1)) ? 1 : x;
    m_origin.y = y;
    m_displayed.clear();
    invalidate();
    invalidate_border();
}

std::shared_ptr<const color_table> display_manager::get_color_table() const
{
    return m_colors;
}

void display_manager::set_color_table(std::shared_ptr<const color_table> colors)
{
    m_colors = colors;
    force_redisplay();
    invalidate_border();
}

void display_manager::set_left_text(const char* left, uint16_t width)
{
    if (m_left_text.set(left, width))
    {
        m_hwheel_exclusion = false;
        invalidate();
    }
}

void display_manager::set_right_text(const char* right, uint16_t width)
{
    if (m_right_text.set(right, width))
        invalidate();
}

void display_manager::set_suggestion_text(const char* suggestion, size_t len)
{
    if (m_suggestion_text.set(suggestion, len))
        invalidate();
}

void display_manager::set_usage_text(const char* usage, uint16_t width)
{
    if (m_usage_text.set(usage, width))
        invalidate();
}

void display_manager::set_message_text(const char* message, uint16_t width)
{
    if (m_message_text.set(message, width))
    {
        m_hwheel_exclusion = false;
        invalidate();
    }
}

void display_manager::set_additional_lines(const std::vector<additional_display_line>& lines)
{
    m_additional_lines = lines;
}

void display_manager::clear_additional_lines()
{
    m_additional_lines.clear();
}

coord display_manager::get_effective_max_size(bool omit_scroll_markers)
{
    assert(m_layout);
    if (!m_layout)
    {
nope:
        return { 0, 0 };
    }

    const border_definition* b = m_style ? m_style->border : nullptr;
    const uint16_t b_left_width = b->get_left_width();
    const uint16_t b_right_width = b->get_right_width();
    const uint16_t extra_border_width = b_left_width + b_right_width;
    const uint16_t b_height = !!b->has_top() + !!b->has_bottom();

    coord max_size;
    max_size.x = m_layout->max_width;
    max_size.y = clamp<int16_t>(m_layout->max_height, 0, m_term_size.y - b_height);
    if (m_origin.x + max_size.x + extra_border_width > m_term_size.x)
    {
        if (m_term_size.x <= m_origin.x + extra_border_width)
            goto nope;
        max_size.x = m_term_size.x - (m_origin.x + extra_border_width - 1);
        if (max_size.x < 8)
            goto nope;
    }
    if (max_size.y <= 0)
        goto nope;

    if (omit_scroll_markers && max_size.y == 1 && m_style->horiz_scroll_markers)
        max_size.x = (max_size.x > c_horz_scroll_indicator_chars) ? max_size.x - c_horz_scroll_indicator_chars : 0;

    assert(max_size.x >= 0);
    assert(max_size.y >= 0);
    return max_size;
}

coord display_manager::get_extent() const
{
    return m_displayed.m_extent;
}

coord display_manager::get_inner_extent() const
{
    const border_definition* b = m_style ? m_style->border : nullptr;
    const uint16_t b_left_width = b->get_left_width();
    const uint16_t b_right_width = b->get_right_width();
    const uint16_t extra_border_width = b_left_width + b_right_width;
    const uint16_t b_height = !!b->has_top() + !!b->has_bottom();

    coord inner_extent = m_displayed.m_extent;
    inner_extent.x -= extra_border_width;
    inner_extent.y -= b_height;
    inner_extent.y -= int32_t(m_displayed.m_additional_lines.size());
    assert(inner_extent.x >= 0);
    assert(inner_extent.y >= 0);
    return inner_extent;
}

void display_manager::clear_scroll_offsets()
{
    set_scroll_offsets(0, 0);
}

void display_manager::set_scroll_offsets(textpos_t left, uint32_t top)
{
    m_left = left;
    m_top = top;
    m_hwheel_exclusion = false;
}

bool display_manager::scroll_horizontally(int32_t columns, int32_t cursor_column, selection_state& selection, bool exclude_auto_scroll)
{
    if (!columns || get_effective_max_size().y != 1)
        return false;

    cursor_column -= m_displayed.m_inner_offset.x;

    const cstring& text = m_buffer->get_text();
    const textpos_t length = textpos_t(text.length());
    textpos_t left = m_left;
    uint32_t remaining = uint32_t(columns < 0 ? -columns : columns);

    if (columns > 0)
    {
        while (left < length && remaining)
        {
            uint16_t width;
            const textpos_t next = forward_one_grapheme(text.c_str(), text.length(), left, &width);
            if (next >= length || remaining < width)
                break;

            textpos_t test = next;
            uint32_t available = 0;
            if (m_style->horiz_scroll_markers)
            {
                test = forward_one_grapheme(text.c_str(), text.length(), test, nullptr);
                available = c_horz_scroll_indicator_chars;
            }
            while (test < length && available < uint32_t(max(cursor_column, 0)))
            {
                uint16_t test_width;
                test = forward_one_grapheme(text.c_str(), text.length(), test, &test_width);
                available += test_width;
            }
            if (available < uint32_t(max(cursor_column, 0)))
                break;

            left = next;
            remaining -= width;
        }
    }
    else
    {
        while (left > 0 && remaining)
        {
            uint16_t width;
            const textpos_t prev = backward_one_grapheme(text.c_str(), text.length(), left, &width);
            if (remaining < width)
                break;
            left = prev;
            remaining -= width;
        }
    }

    textpos_t caret = left;
    uint32_t screen_column = 0;
    if (left && m_style->horiz_scroll_markers)
    {
        caret = forward_one_grapheme(text.c_str(), text.length(), left, nullptr);
        screen_column = c_horz_scroll_indicator_chars;
    }

    while (caret < length && screen_column < uint32_t(max(cursor_column, 0)))
    {
        uint16_t width;
        const textpos_t next = forward_one_grapheme(text.c_str(), text.length(), caret, &width);
        if (screen_column + width > uint32_t(cursor_column))
            break;
        caret = next;
        screen_column += width;
    }

    const bool changed = left != m_left || caret != selection.get_caret();
    m_left = left;
    selection.set_caret(caret);
    if (exclude_auto_scroll)
        suppress_auto_horizontal_scroll(selection);
    return changed;
}

void display_manager::suppress_auto_horizontal_scroll(const selection_state& selection)
{
    if (get_effective_max_size().y != 1)
        return;

    m_hwheel_exclusion = true;
    m_hwheel_exclusion_left = m_left;
    m_hwheel_exclusion_caret = selection.get_caret();
    m_hwheel_exclusion_change_counter = m_buffer->get_change_counter();
}

bool display_manager::move_caret_vertically(int32_t rows, int32_t cursor_column, selection_state& selection, bool select)
{
    const coord max_size = get_effective_max_size();
    if (!rows || max_size.y <= 1 || !m_displayed.m_change_counter)
        return false;

    // REVIEW: this seems more complicated and convoluted than necessary.

    const int32_t current_row = m_displayed.m_top + m_displayed.m_cursor.y;
    const int32_t wanted_row = max(current_row + rows, 0);
    const cstring& text = m_buffer->get_text();
    display_row_start target = { 0, false };
    display_row_start last = target;
    int32_t row = 0;
    const uint16_t left_text_width =
        (m_left_text.length() && m_left_text.width() < max_size.x) ? m_left_text.width() : 0;
    uint16_t row_width = left_text_width;
    wcwidth_iter scan(text.c_str(), text.length());

    while (scan.more() && row < wanted_row)
    {
        scan.next();
        const char* const p = scan.character_pointer();
        const textpos_t offset = textpos_t(p - text.c_str());
        if (scan.character_wcwidth_signed() < 0)
        {
            if (*p == '\n')
            {
                last = { textpos_t(offset + scan.character_length()), false };
                ++row;
                row_width = 0;
                continue;
            }
            if (row_width + 1 > uint16_t(max_size.x))
            {
                last = { offset, false };
                ++row;
                row_width = 0;
                if (row >= wanted_row)
                    break;
            }
            ++row_width;
            if (row_width + 1 > uint16_t(max_size.x))
            {
                last = { offset, true };
                ++row;
                row_width = 0;
                if (row >= wanted_row)
                    break;
            }
            ++row_width;
        }
        else
        {
            const uint16_t width = scan.character_wcwidth_twoctrl();
            if (row_width + width > uint16_t(max_size.x))
            {
                last = { offset, false };
                ++row;
                row_width = 0;
                if (row >= wanted_row)
                    break;
            }
            row_width += width;
        }
    }

    target = last;
    if (row < wanted_row && row_width == uint16_t(max_size.x))
    {
        target = { textpos_t(text.length()), false };
        ++row;
    }

    cursor_column = max(cursor_column - m_displayed.m_inner_offset.x, 0);
    textpos_t caret = target.offset;
    uint32_t screen_column = (wanted_row == 0) ? left_text_width : 0;
    if (target.pending)
    {
        if (cursor_column)
        {
            caret = forward_one_grapheme(text.c_str(), text.length(), caret, nullptr);
            screen_column = 1;
        }
    }
    while (size_t(caret) < text.length() && screen_column < uint32_t(cursor_column))
    {
        wcwidth_iter iter(text.c_str() + caret, text.length() - caret);
        const char32_t c = iter.next();
        if (c == '\n')
            break;
        const uint32_t width = iter.character_wcwidth_twoctrl();
        if (screen_column + width > uint32_t(cursor_column))
            break;
        caret += iter.character_length();
        screen_column += width;
    }

    const bool changed = caret != selection.get_caret();
    if (select)
        selection.set_selection(selection.get_anchor(), caret);
    else
        selection.set_caret(caret);
    return changed;
}

bool display_manager::get_pos_from_screen(uint32_t x, uint32_t y, textpos_t& pos, screen_scroll_info* scroll)
{
    if (!m_displayed.m_change_counter)
        return false;

    const border_definition* const border = m_style ? m_style->border : nullptr;
    int32_t screen_origin_x = m_origin.x;
    int32_t screen_origin_y = m_origin.y;
#ifdef _WIN32
    // BUGBUG: when the console wrapping is off and the console window is
    // narrower than the console buffer, the origin X is handled wrongly.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return false;
    screen_origin_x = csbi.dwCursorPosition.X - csbi.srWindow.Left + 1 - m_relative_cursor.x;
    screen_origin_y = csbi.dwCursorPosition.Y - csbi.srWindow.Top + 1 - m_relative_cursor.y;
#else
    if (screen_origin_y <= 0)
        return false;
#endif
    const int32_t left = screen_origin_x + m_displayed.m_inner_offset.x;
    const int32_t top = screen_origin_y + m_displayed.m_inner_offset.y;
    const int32_t width = m_displayed.m_extent.x - m_displayed.m_inner_offset.x - border->get_right_width();
    const int32_t height = m_displayed.m_extent.y - int32_t(m_displayed.m_additional_lines.size()) -
                           m_displayed.m_inner_offset.y - !!border->has_bottom();
    const bool multiline = get_effective_max_size().y > 1;
    int32_t column = int32_t(x) - left;
    int32_t row = int32_t(y) - top;

    if (scroll)
    {
        *scroll = {};
        if (multiline && (row < 0 || row >= height))
        {
            scroll->direction = row < 0 ? screen_scroll_direction::up : screen_scroll_direction::down;
            scroll->cursor_column = clamp(column, 0, width - 1) + m_displayed.m_inner_offset.x;
            const int32_t current_row = m_displayed.m_top + m_displayed.m_cursor.y;
            const int32_t target_row = row < 0 ? m_displayed.m_top - 1 : m_displayed.m_top + height;
            scroll->vertical_row_delta = target_row - current_row;
            return true;
        }
        if (!multiline)
        {
            row = 0;
            const bool over_left_indicator = m_left && m_style && m_style->horiz_scroll_markers &&
                                             column < c_horz_scroll_indicator_chars;
            if (column < 0 || over_left_indicator)
            {
                scroll->direction = screen_scroll_direction::left;
                scroll->cursor_column = m_displayed.m_inner_offset.x;
                return true;
            }

            const display_line* const line = m_displayed.m_lines.empty() ? nullptr : m_displayed.m_lines[0].get();
            const bool has_right_indicator = line && line->m_faces.length() &&
                                             line->m_faces.c_str()[line->m_faces.length() - 1] == FACE_SCROLLER;
            const bool over_right_indicator = has_right_indicator &&
                                              column >= width - c_horz_scroll_indicator_chars;
            if (column >= width || over_right_indicator)
            {
                scroll->direction = screen_scroll_direction::right;
                scroll->cursor_column = m_displayed.m_inner_offset.x + width - 1;
                return true;
            }
        }
    }

    if (scroll && multiline && row == 0 && m_displayed.m_top > 0 && column < 0)
        column = 0;
    else if (scroll && multiline && row == height - 1 && column >= width &&
             !m_displayed.m_lines.empty() && m_displayed.m_lines.back()->m_trail_scroller_width_displaced)
        column = width - 1;

    if (column < 0 || column >= width || row < 0 || row >= height || size_t(row) >= m_displayed.m_rows.size())
        return false;

    const cstring& text = m_buffer->get_text();
    const display_row_start& start = m_displayed.m_rows[row];

    // If the row contains no input text, return the end of the input text.
    if (start.offset == c_padding_row_offset || start.virtual_text || size_t(start.offset) >= text.length())
    {
        pos = textpos_t(text.length());
        return true;
    }

    pos = start.offset;
    uint32_t screen_column = 0;

    if (row == 0 && m_displayed.m_left_text.width())
    {
        screen_column = m_displayed.m_left_text.width();
        if (uint32_t(column) < screen_column)
            return true;
    }

    if (start.pending)
    {
        if (!column)
            return true;
        pos = forward_one_grapheme(text.c_str(), text.length(), pos);
        screen_column = 1;
    }
    else if (m_left && m_style && m_style->horiz_scroll_markers)
    {
        if (column < c_horz_scroll_indicator_chars)
            return true;
        pos = forward_one_grapheme(text.c_str(), text.length(), pos);
        screen_column = c_horz_scroll_indicator_chars;
    }

    while (size_t(pos) < text.length() && screen_column <= uint32_t(column))
    {
        wcwidth_iter iter(text.c_str() + pos, text.length() - pos);
        const char32_t c = iter.next();
        if (c == '\n' && multiline)
            break;

        const uint32_t char_width = iter.character_wcwidth_twoctrl();
        if (screen_column + char_width > uint32_t(column) || screen_column + char_width > uint32_t(width))
            break;

        pos += iter.character_length();
        screen_column += char_width;
    }

    return true;
}

bool display_manager::set_caret_from_screen(uint32_t x, uint32_t y, selection_state& selection, uint32_t drag_scroll_chars, bool word_drag)
{
    textpos_t pos;
    if (!get_pos_from_screen(x, y, pos))
        return false;
    selection.set_caret(pos);
    return true;
}

void display_manager::ensure_left()
{
    // REVIEW: this smells overly complicated; I suspect that Codex may have
    // made a mess of this method and it might be worth manually rewriting it.

    const coord max_size = get_effective_max_size(true/*omit_scroll_markers*/);
    if (max_size.y != 1)
    {
        m_left = 0;
        return;
    }

    const selection_state& selection = m_buffer->get_selection_state();
    bool suppress_auto_scroll = false;
    if (m_hwheel_exclusion)
    {
        if (m_left == m_hwheel_exclusion_left &&
            selection.get_caret() == m_hwheel_exclusion_caret &&
            m_buffer->get_change_counter() == m_hwheel_exclusion_change_counter)
        {
            suppress_auto_scroll = true;
        }
        else
        {
            m_hwheel_exclusion = false;
        }
    }

    m_left = min(m_left, selection.get_caret());

    const coord full_max_size = get_effective_max_size();
    const uint16_t left_text_width = (m_left_text.length() && m_left_text.width() < full_max_size.x) ? m_left_text.width() : 0;
    const cstring& text = m_buffer->get_text();

    // Auto-scroll horizontally backward.
    if (!suppress_auto_scroll)
    {
        const textpos_t backup_left = back_up_by_amount(selection.get_caret(), text.c_str(), selection.get_caret(), 4);
        if (m_left > backup_left)
            m_left = backup_left;
    }

    // Auto-scroll horizontally forward.
    const uint32_t caret_offset = uint32_t(selection.get_caret() - m_left);
    parse_graphemes(text.c_str() + m_left, text.length() - m_left, caret_offset, m_tmp_graphemes);

    int16_t width = 0;
    uint32_t line_width = left_text_width;
    for (const auto& g : m_tmp_graphemes)
    {
        line_width += g.width;
        if (g.index + g.length <= caret_offset)
            width += g.width;
    }
    const bool unscrolled_right_marker = line_width > uint32_t(full_max_size.x);

    for (auto g = m_tmp_graphemes.cbegin(); true; ++g)
    {
        const uint16_t current_left_text_width = m_left ? 0 : left_text_width;
        // Reserve the right marker when the unscrolled line needs one;
        // otherwise reserve only the terminal cell needed by the caret.
        const int16_t caret_max_width = (m_left || unscrolled_right_marker) ? max_size.x : full_max_size.x - 1;
        int16_t display_width = width;
        if (m_left && m_style->horiz_scroll_markers && g != m_tmp_graphemes.cend())
            display_width = get_horiz_scrolled_width(width, g->width);
        if (display_width + current_left_text_width < caret_max_width)
            break;

        assert(g != m_tmp_graphemes.cend());
        width -= g->width;
        m_left += g->length;
    }
    assert(selection.get_caret() >= m_left);
}

void display_manager::begin_display()
{
    m_displayed.clear();
    m_relative_cursor = { -1, 0 };
    m_display_ended = false;
    force_redisplay();
    invalidate_border();
}

bool display_manager::display()
{
    ++s_display.total;

    assert(is_initialized());
    if (!is_initialized())
    {
        ++s_display.error;
        return false;
    }

    // Is there nothing to display?
    if (m_display_ended || !m_buffer->get_change_counter())
    {
        ++s_display.nop;
        return false;
    }

    // If origin not set yet, then pin it "here".
    if (m_origin.x <= 0)
    {
        m_origin.x = 1;
        m_origin.y = -1;
        m_relative_cursor = { -1, 0 };   // Cursor is relative to origin.
    }

    // Update awareness of the terminal size.
    const coord term_size = get_terminal_size();
    if (m_term_size != term_size)
    {
        invalidate();
        invalidate_border();
        m_term_size = term_size;
    }

    ensure_left();

    // If only the caret has changed, then the cursor can simply be updated.
    if (try_update_caret_only())
    {
        ++s_display.nop;
        return false;
    }

    // Format content into display structures.
    display_lines tmp;
    if (!build(tmp))
    {
        ++s_display.nop;
        DISPLAY_STATISTICS();
        return false;   // Nothing changed since last display (or OOM error).
    }

    return display_internal(tmp);
}

void display_manager::force_redisplay()
{
    invalidate();
    m_force_redisplay = true;
}

void display_manager::print_text_with_faces(coord& cursor, const char* t, const char* f, size_t len)
{
    char face = 0;
    while (len > 0)
    {
        if (*f != face)
        {
            output_color(get_face_def(*f));
            face = *f;
        }

        wcwidth_iter iter(t, len);
        if (!iter.more())
            break;

        const char32_t c = iter.next();
        const uint32_t clen = iter.character_length();
        assert(clen <= len);

        if (c == 0xfffd)
            output(c_replacement_character);
        else
            output(iter.character_pointer(), clen);

        t += clen;
        f += clen;
        len -= clen;
        cursor.x += iter.character_wcwidth_twoctrl();
    }
}

bool display_manager::try_update_caret_only()
{
    if (m_invalidated ||
        !m_displayed.m_change_counter ||
        m_buffer->get_change_counter() != m_displayed.m_change_counter ||
        m_displayed.m_anchor != m_displayed.m_pos ||
        m_left != m_displayed.m_left ||
        int32_t(m_top) != m_displayed.m_top ||
        m_additional_lines != m_displayed.m_additional_lines ||
        m_displayed.m_rows.empty())
        return false;

    const selection_state& selection = m_buffer->get_selection_state();
    const textpos_t caret = selection.get_caret();
    if (selection.has_selection() ||
        caret == m_displayed.m_pos)
        return false;

    const cstring& text = m_buffer->get_text();
    if (size_t(caret) > text.length())
        return false;

    // Calculate the row containing the caret.
    size_t row = 0;
    while (row + 1 < m_displayed.m_rows.size() &&
           !m_displayed.m_rows[row + 1].virtual_text &&
           m_displayed.m_rows[row + 1].offset <= caret)
        ++row;

    const display_row_start* start = &m_displayed.m_rows[row];
    if (caret < start->offset)
        return false;

    // A pending row begins with the second displayed cell of a control
    // character.  The caret at its byte offset belongs before the control
    // character, on the preceding row.
    if (start->pending && caret == start->offset)
    {
        if (!row)
            return false;
        start = &m_displayed.m_rows[--row];
    }

    const coord max_size = get_effective_max_size();
    if (max_size.x <= 0 || max_size.y <= 0)
        return false;
    const bool multiline = (max_size.y > 1);

    uint32_t column = (m_displayed.m_top + row == 0) ? m_displayed.m_left_text.width() : 0;
    textpos_t pos = start->offset;
    if (start->pending)
    {
        pos = forward_one_grapheme(text.c_str(), text.length(), pos, nullptr);
        column = 1;
    }

    while (pos < caret)
    {
        uint16_t width;
        const textpos_t next = forward_one_grapheme(text.c_str(), text.length(), pos, &width);
        if (next <= pos)
            return false;
        if (next > caret)
            break;
        if (multiline && text.c_str()[pos] == '\n')
            return false;
        if (column + width > uint32_t(max_size.x))
            return false;
        column += width;
        pos = next;
    }

    if (!multiline && m_left && m_style->horiz_scroll_markers)
    {
        uint16_t replaced_width;
        forward_one_grapheme(text.c_str(), text.length(), m_left, &replaced_width);
        column = get_horiz_scrolled_width(uint16_t(column), replaced_width);
    }
    if (column >= uint32_t(max_size.x))
        return false;

    // A full build moves the viewport rather than putting the caret under a
    // multiline scroll marker.  Defer to it in those cases.
    if (multiline && m_displayed.m_top && !row && column < c_horz_scroll_indicator_chars)
        return false;
    if (multiline && row + 1 == m_displayed.m_rows.size())
    {
        const display_line& line = *m_displayed.m_lines[row];
        if (line.m_trail_scroller_width_displaced &&
            column >= uint32_t(line.width() - line.m_trail_scroller_width_displaced))
            return false;
    }

    m_displayed.m_pos = caret;
    m_displayed.m_anchor = caret;
    m_displayed.m_cursor = { int32_t(column), int32_t(row) };

    init_horizpos_workaround();
    move_to_caret_position();
    return true;
}

bool display_manager::display_internal(display_lines& lines)
{
    bool any_updates = false;

    if (lines.m_erase)
    {
        assert(lines.m_lines.empty());
        assert(lines.m_additional_lines.empty());
        lines.m_extent.x = m_displayed.m_extent.x;
        lines.m_extent.y = 0;
    }

    const coord input_extent = { lines.m_extent.x, lines.m_extent.y - int32_t(lines.m_additional_lines.size()) };
    const coord displayed_input_extent = { m_displayed.m_extent.x, m_displayed.m_extent.y - int32_t(m_displayed.m_additional_lines.size()) };
    assert(input_extent.y >= 0);
    assert(displayed_input_extent.y >= 0);
    if (m_force_redisplay || input_extent != displayed_input_extent)
        m_border_dirty = true;

    init_horizpos_workaround();
    preserve_window_horiz_scroll_position preserve(m_horizpos_workaround, this);

    m_accumulator.clear();
    m_coalesce_output = g_coalesce_output;

#ifdef _WIN32
    m_pending_wrap = false;
    m_pending_wrap_display = &lines;
    // FUTURE: force_wrap: if prompt text above the display_lines ends with a
    // pending wrap, then m_pending_wrap needs to be forced true here.
#endif

    coord cursor = m_relative_cursor;
    const coord term_size = m_term_size;
    const coord max_size = get_effective_max_size();

    if (cursor.x < 0 && cursor.y < 0)
        cursor = { -1, 0 };             // -1 forces move_to_column.

    auto erase_row = [&](int16_t width)
    {
        if (width <= 0)
            return;
        if (m_origin.x + cursor.x + width - 1 >= term_size.x)
        {
            clr_to_eol(term_size.x - (m_origin.x + cursor.x) + 1);
        }
        else
        {
            output_spaces(width);
            cursor.x += width;
        }
    };

    if (g_show_hide_cursor)
        output(c_hide_cursor);

#ifdef DEBUG
    {
        tib::cstring v;
        if (tib::getenv("TIB_SHOW_INCREMENTAL_UPDATES", v) && !v.empty())
        {
            move_to_row(cursor, 0, 0);
            move_to_column(cursor, 0, 0);
            output("\x1b[J");
        }
    }
#endif

    // Draw border if needed.
    if (!lines.m_erase && m_style && m_style->border && m_border_dirty)
    {
        any_updates = true;
        move_to_row(cursor, 0, 0);
        move_to_column(cursor, 0, 0);
        append_border(input_extent);
    }
    m_border_dirty = false;

    // Display the lines.
    for (uint16_t i = 0; i < lines.m_lines.size(); ++i)
    {
        auto const& line = lines.m_lines[i];

        ++s_display.row_total;

#ifdef _WIN32
        const bool can_optimize = !m_horizpos_workaround;
#else
        const bool can_optimize = true;
#endif

        // Does the new line exactly match the previously displayed line?
        size_t begin = 0;
        size_t end = line->m_text.length();
        uint16_t begin_width = 0;
        bool reuse_left_text = false;
        bool reuse_right_text = false;
        int16_t right_gap_dirty_width = int16_max;
        int16_t rest_dirty_width = int16_max;
        if (!m_force_redisplay && can_optimize && i < m_displayed.m_lines.size())
        {
            const auto& displayed = m_displayed.m_lines[i];
            const bool has_right_text = int32_t(i) == lines.m_right_text_row && lines.m_right_text.length();
            const bool had_right_text = int32_t(i) == m_displayed.m_right_text_row && m_displayed.m_right_text.length();
            reuse_left_text = !(i == 0 && !(lines.m_left_text == m_displayed.m_left_text));
            reuse_right_text = (!has_right_text && !had_right_text) ||
                               (has_right_text && had_right_text && lines.m_right_text == m_displayed.m_right_text);
            if (input_extent.x == displayed_input_extent.x)
            {
                rest_dirty_width = (displayed->width() > line->width() ?
                                    displayed->width() - line->width() : 0);

                // If this row owns the effective right-aligned text, prepare to
                // clear any stale portion of the gap and text.
                if (had_right_text)
                {
                    if (m_displayed.m_right_text.width() <= lines.m_right_text.width())
                        right_gap_dirty_width = rest_dirty_width;

                    const auto consumed_width = (displayed->width() +
                                                 c_right_text_padding +
                                                 m_displayed.m_right_text.width());
                    if (consumed_width <= displayed_input_extent.x)
                        rest_dirty_width = max_size.x - line->width();
                }
            }
            if (displayed->m_x1 == line->m_x1)
            {
                // First do a simple memcmp comparison to check if the new
                // line exactly matches the previously displayed line.  The
                // grapheme comparison for a whole line is more than 3 orders
                // of magnitude slower than the memcmp comparison.
                if (reuse_left_text &&
                    line->m_text.equals(displayed->m_text) &&
                    line->m_faces.equals(displayed->m_faces) &&
                    reuse_right_text)
                {
                    ++s_display.row_nop;
                    continue;
                }

                const size_t limit_forward_skip = min(displayed->m_text.length(), line->m_text.length());

                // Walk forward past a leading portion that exactly matches.
                size_t displayed_begin = 0;
                const size_t displayed_length = displayed->m_text.length();
                while (reuse_left_text && begin < end && displayed_begin < limit_forward_skip)
                {
                    uint16_t width;
                    const size_t next = forward_one_grapheme(line->m_text.c_str(), end, uint32_t(begin), &width);
                    const size_t displayed_next = forward_one_grapheme(displayed->m_text.c_str(), displayed_length, uint32_t(displayed_begin));
                    const size_t length = next - begin;
                    const size_t displayed_grapheme_length = displayed_next - displayed_begin;
                    if (length != displayed_grapheme_length ||
                        memcmp(line->m_text.c_str() + begin, displayed->m_text.c_str() + displayed_begin, length) != 0 ||
                        memcmp(line->m_faces.c_str() + begin, displayed->m_faces.c_str() + displayed_begin, length) != 0)
                        break;

                    begin = next;
                    displayed_begin = displayed_next;
                    begin_width += width;
                }

                // Walk backward past a trailing portion that exactly matches.
                // REVIEW: Instead of giving up if the x2 columns differ, it
                // could walk backwards to find the greatest column less than
                // or equal to `displayed->m_x2` that is a grapheme boundary
                // for both `displayed` and `line`.  It could always print
                // everything starting from there, but it could also walk
                // backwards from there to find a shorter middle region to
                // update.  But is that really a common enough case to justify
                // the extra complexity and effort?
                // REVIEW: But if the text is the full terminal width, then it
                // could use ICH (CSI Ps @) and DCH (CSI Ps P) to shift
                // characters instead of printing the whole line.  I'm not
                // sure how much it really matters for performance, but it
                // could reduce the number of bytes printed to the terminal.
                if (displayed->m_x2 == line->m_x2)
                {
                    size_t displayed_end = displayed_length;
                    while (end > begin && displayed_end > displayed_begin)
                    {
                        const size_t previous = backward_one_grapheme(line->m_text.c_str(), line->m_text.length(), uint32_t(end));
                        const size_t displayed_previous = backward_one_grapheme(displayed->m_text.c_str(), displayed_length, uint32_t(displayed_end));
                        const size_t length = end - previous;
                        const size_t displayed_grapheme_length = displayed_end - displayed_previous;
                        if (length != displayed_grapheme_length ||
                            memcmp(line->m_text.c_str() + previous, displayed->m_text.c_str() + displayed_previous, length) != 0 ||
                            memcmp(line->m_faces.c_str() + previous, displayed->m_faces.c_str() + displayed_previous, length) != 0)
                            break;

                        end = previous;
                        displayed_end = displayed_previous;
                    }
                }
            }
        }

        any_updates = true;

        // Move the cursor to the start of the text to display.
        move_to_row(cursor, i, lines.m_inner_offset.y);
        const uint16_t left_text_width = (i == 0) ? lines.m_left_text.width() : 0;

        // The left text is kept separate from the input text because it may
        // contain terminal escape sequences whose width the caller attests.
        // BUGBUG: when the console wrapping is off and the console window is
        // narrower than the console buffer, the origin X is handled wrongly.
        if (i == 0 && !reuse_left_text && begin == 0 && lines.m_left_text.length())
        {
            move_to_column(cursor, 0, lines.m_inner_offset.x);
            output(lines.m_left_text.c_str(), lines.m_left_text.length());
            cursor.x += left_text_width;
        }
        else
        {
            move_to_column(cursor, begin_width + left_text_width, lines.m_inner_offset.x);
        }

        // Display the text.
        print_text_with_faces(cursor, line->m_text.c_str() + begin, line->m_faces.c_str() + begin, end - begin);

        // Fill remaining width.
        if (line->width() < max_size.x)
        {
            const bool has_right_text =
                int32_t(i) == lines.m_right_text_row &&
                lines.m_right_text.width() &&
                line->width() + c_right_text_padding + lines.m_right_text.width() <= max_size.x;

            if (has_right_text)
            {
                // Clear gap without disturbing reusable right-aligned text.
                const int16_t gap_width = max_size.x - (line->width() + lines.m_right_text.width());
                const int16_t erase_width = min(gap_width, right_gap_dirty_width);
                if (erase_width > 0 || !reuse_right_text)
                    output_color(get_face_def(m_style ? m_style->empty_face : FACE_EMPTY));
                if (erase_width > 0)
                {
                    move_to_column(cursor, line->width(), lines.m_inner_offset.x);
                    erase_row(min(gap_width, right_gap_dirty_width));
                }
                if (!reuse_right_text)
                {
                    move_to_column(cursor, max_size.x - lines.m_right_text.width(), lines.m_inner_offset.x);
                    output(lines.m_right_text.c_str(), lines.m_right_text.length());
                }
            }
            else
            {
                // Clear stale text through the rest of the row.
                const int16_t rest_width = max_size.x - line->width();
                const int16_t erase_width = min(rest_width, rest_dirty_width);
                if (erase_width > 0)
                {
                    move_to_column(cursor, line->width(), lines.m_inner_offset.x);
                    output_color(get_face_def(m_style ? m_style->empty_face : FACE_EMPTY));
                    erase_row(erase_width);
                }
            }
        }

        ++s_display.row_action;

#ifdef _WIN32
        // Update cursor position and deal with autowrap.
        detect_pending_wrap(cursor);
#endif
    }

    // Display additional lines, comparing by terminal row rather than by
    // vector index so lines can be reused when the input height changes.
    const int32_t additional_begin = input_extent.y;
    const int32_t displayed_additional_begin = displayed_input_extent.y;
    for (size_t i = 0; i < lines.m_additional_lines.size(); ++i)
    {
        const int32_t row = additional_begin + int32_t(i);
        const additional_display_line& line = lines.m_additional_lines[i];

        ++s_display.addl_total;

        // If the displayed line ends up the same then skip displaying it.
        const additional_display_line* displayed = nullptr;
        if (row >= displayed_additional_begin && row < m_displayed.m_extent.y)
            displayed = &m_displayed.m_additional_lines[row - displayed_additional_begin];
        if (!m_force_redisplay)
        {
            const bool reuse_displayed_line = displayed && line == *displayed &&
                (!line.bounded || input_extent.x == displayed_input_extent.x);
            if (reuse_displayed_line)
            {
                ++s_display.addl_nop;
                continue;
            }
        }
        any_updates = true;

        // Move the cursor.  Be sure to restore the default color first,
        // otherwise if the terminal scrolls it will fill the new line with
        // the wrong color.
        output_color("");
        move_to_row(cursor, uint16_t(row), 0);
        const bool erased_unbounded_line = line.bounded && displayed && !displayed->bounded;
        if (erased_unbounded_line)
        {
            output(term_col(1));
            cursor.x = 1 - m_origin.x;
            clr_to_eol(term_size.x);
        }
        if (line.bounded)
        {
            move_to_column(cursor, 0, 0);
        }
        else
        {
            output(term_col(1));
            cursor.x = 1 - m_origin.x;
        }

        // Print the line text if it fits.
        const uint16_t max_width = (line.bounded) ? input_extent.x : term_size.x;
        const bool line_overflow = line.width > max_width;
        const uint16_t width = line_overflow ? 0 : line.width;
        if (!line_overflow)
        {
            output(line.text.c_str(), line.text.length());
            cursor.x += line.width;
        }

        // Pad/clear to the appropriate width.
        if (line.bounded)
        {
            if (!line_overflow || !erased_unbounded_line)
            {
                erase_row(input_extent.x - width);
            }
        }
        else if (line.width < term_size.x)
        {
            clr_to_eol(term_size.x - line.width);
        }

        ++s_display.addl_action;

#ifdef _WIN32
        // Update cursor position and deal with autowrap.
        detect_pending_wrap(cursor);
#endif
    }

    // Erase rows in m_displayed but not in lines.
    if (lines.m_extent.y < m_displayed.m_extent.y)
    {
        any_updates = true;
        output_color("");
        for (uint16_t i = lines.m_extent.y; i < m_displayed.m_extent.y; ++i)
        {
            move_to_row(cursor, i, 0);
            const bool unbounded = i >= displayed_additional_begin &&
                !m_displayed.m_additional_lines[i - displayed_additional_begin].bounded;
            if (unbounded)
            {
                output(term_col(1));
                cursor.x = 1 - m_origin.x;
                output(term_erase_to_eol());
            }
            else
            {
                move_to_column(cursor, 0, 0);
                erase_row(displayed_input_extent.x);
            }
        }
    }

    // Position cursor at the caret position.
    if (lines.m_erase)
        lines.m_cursor = { 0, 0 };
    move_to_row(cursor, lines.m_cursor.y, lines.m_inner_offset.y);
    move_to_column(cursor, lines.m_cursor.x, lines.m_inner_offset.x);

    output_color("");
    if (g_show_hide_cursor)
        output(c_show_cursor);

#ifdef _WIN32
    assert(!m_pending_wrap);
    m_pending_wrap_display = nullptr;
#endif

    if (m_coalesce_output)
    {
        m_coalesce_output = false;
        maybe_flush();
    }

    ++s_display.action;

    m_top = lines.m_top;
    m_displayed = std::move(lines);
    m_relative_cursor = cursor;
    m_invalidated = false;
    m_force_redisplay = false;

    DISPLAY_STATISTICS();
    return any_updates;
}

void display_manager::erase_display()
{
    if (m_displayed.m_extent.y > 0)
    {
        display_lines tmp;
        tmp.m_erase = true;

        ++s_display.total;
        display_internal(tmp);
    }
}

void display_manager::end_display_lf()
{
    if (m_display_ended)
        return;

    display();

    // A final row used only for the caret already supplies the line break.
    if (m_displayed.m_phantom_last_row)
        --m_displayed.m_extent.y;

    // TODO: coalesce...  (maybe even nested coalesce around display()).
    move_to_end_of_display();
    move_to_column(m_relative_cursor, 0, 0);
    // do_flush();

    m_displayed.clear();
    m_relative_cursor = { -1, 0 };
    m_origin = { -1, -1 };
    m_hwheel_exclusion = false;
#ifdef _WIN32
    m_pending_wrap = false;
    m_pending_wrap_display = nullptr;
#endif

    // Completed output belongs to the terminal until begin_display().
    m_display_ended = true;
}

void display_manager::move_to_end_of_display()
{
    if (m_displayed.m_extent.y > 0)
    {
        move_to_row(m_relative_cursor, m_displayed.m_extent.y, 0);
    }
}

void display_manager::move_to_caret_position()
{
    if (m_displayed.m_extent.y > 0)
    {
        move_to_row(m_relative_cursor, m_displayed.m_cursor.y, m_displayed.m_inner_offset.y);
        move_to_column(m_relative_cursor, m_displayed.m_cursor.x, m_displayed.m_inner_offset.x);
    }
}

void display_manager::move_to_row(coord& cursor, uint16_t y, uint16_t inner_offset)
{
#ifdef _WIN32
    if (m_pending_wrap)
        finish_pending_wrap(cursor);
#endif

    y += inner_offset;

    if (y == cursor.y)
        return;

#ifdef _WIN32
    preserve_window_horiz_scroll_position preserve(m_horizpos_workaround, this);
#endif

    if (y < cursor.y)
    {
        if (m_origin.y > 0)
        {
            output(term_row_col(m_origin.y + y, 1));
            cursor.x = 1 - m_origin.x;
        }
        else
        {
            output(term_move_up(cursor.y - y));
        }
    }
    else if (y > cursor.y)
    {
        for (uint16_t n = y - cursor.y; n--;)
            output("\r\n", 2);
        cursor.x = 1 - m_origin.x;
        // REVIEW: AI thinks making m_origin.y go negative is appropriate
        // when (m_origin.y > 0 && m_origin.y + cursor.y > m_term_size.y).
        // But I'm not convinced yet...
    }
    else
    {
        assert(false);
    }

    cursor.y = y;
}

void display_manager::move_to_column(coord& cursor, uint16_t x, uint16_t inner_offset)
{
#ifdef _WIN32
    if (m_pending_wrap)
        finish_pending_wrap(cursor);
#endif

    x += inner_offset;
    const uint16_t term_x = m_origin.x + x;

#ifdef _WIN32
    if (m_horizpos_workaround)
    {
        do_flush();

        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(m_horizpos_workaround, &csbi);
        csbi.dwCursorPosition.X = term_x - 1;

        preserve_window_horiz_scroll_position preserve(m_horizpos_workaround, this);
        SetConsoleCursorPosition(m_horizpos_workaround, csbi.dwCursorPosition);
    }
    else
#endif _WIN32
    {
        if (term_x > 0)
            output(term_col(term_x));
        else
            output("\r");
    }

    cursor.x = x;
}

const char* display_manager::get_face_def(char face) const
{
    if (!m_face_defs)
    {
default_colors:
        switch (face)
        {
        case FACE_SELECTION:    return m_colors->get_color(tib::color_element::input_selection);
        case FACE_MARK:         return m_colors->get_color(tib::color_element::input_mark);
        case FACE_SCROLLER:     return m_colors->get_color(tib::color_element::input_scroller);
        }
        return m_colors->get_color(tib::color_element::base);
    }

    const auto def = m_face_defs->find(face);
    if (def == m_face_defs->end())
        goto default_colors;

    return def->second.c_str();
}

bool display_manager::build(display_lines& out)
{
    ++s_build.total;

    assert(is_initialized());
    if (!is_initialized())
    {
        ++s_build.error;
        return false;
    }

    // NOTE:  Terminal size change is noted inside get_effective_max_size()
    // inside editor_context::ensure_left() inside editor_context::display().
    // Waking immediately upon terminal resize requires a custom input hook,
    // but even the default input routine will at least allow redisplay the
    // next time some input becomes available.

    const uint32_t change_counter = m_buffer->get_change_counter();
    const selection_state& sel_state = m_buffer->get_selection_state();
    const textpos_t sel_begin = sel_state.get_sel_begin();
    const textpos_t sel_end = sel_state.get_sel_end();
    const textpos_t pos = sel_state.get_caret();
    const textpos_t anchor = sel_state.get_anchor();
    const textpos_t left = m_left;

    if (!m_invalidated &&
        change_counter == m_displayed.m_change_counter &&
        pos == m_displayed.m_pos &&
        anchor == m_displayed.m_anchor &&
        left == m_displayed.m_left &&
        m_additional_lines == m_displayed.m_additional_lines)
    {
        ++s_build.nop;
        return false;
    }

    const cstring& text = m_buffer->get_text();
    const size_t real_text_len = text.length();

    cstring faces;
    faces.append_spaces(text.length());     // FACE_DEFAULT == space.
    if (m_callbacks)
    {
        m_callbacks->provide_faces(*m_buffer, faces);

        assert(text.length() == faces.length());
        if (faces.length() < text.length())
            faces.append_spaces(text.length() - faces.length());
    }

    // If a suggestion string has been provided, temporarily append it now.
    const auto tmp_suggest = m_buffer->scoped_raw_append(m_suggestion_text.c_str());
    if (text.length() > real_text_len)
    {
        const size_t old_length = faces.length();
        memset(faces.reserve(text.length()) + old_length, FACE_SUGGESTION, text.length() - old_length);
        faces.set_length(text.length());
    }

    // Overlay active mark color into faces.
    if (sel_state.is_mark_active())
    {
        const textpos_t mark = sel_state.get_mark();
        const textpos_t marked_begin = min(mark, pos);
        const textpos_t marked_end = max(mark, pos);
        memset(faces.reserve(0) + marked_begin, FACE_MARK, marked_end - marked_begin);
    }

    // Overlay selection color into faces.
    memset(faces.reserve(0) + sel_begin, FACE_SELECTION, sel_end - sel_begin);

    display_lines tmp;
    tmp.m_pos = pos;
    tmp.m_anchor = anchor;
    tmp.m_left = left;
    tmp.m_change_counter = change_counter;

    // Set up border.
    coord term_size = m_term_size;
    if (m_style && m_style->border)
    {
        const border_definition& b = *m_style->border;
        const uint16_t b_height = !!b.has_top() + !!b.has_bottom();
        tmp.m_inner_offset.y = b.has_top() ? 1 : 0;
        tmp.m_inner_offset.x = b.get_left_width();
        tmp.m_extent.x += b.get_left_width() + b.get_right_width();
        tmp.m_extent.y += b_height;
        term_size.y -= b_height;
    }

    // Set up max height.
    const coord max_size = get_effective_max_size();
    const coord max_size_omit_scroll_markers = get_effective_max_size(true/*omit_scroll_markers*/);
    if (max_size.y < 1)
    {
        ++s_build.error;
        return false;
    }
    const bool multiline = (max_size.y > 1);
    assert(implies(multiline, !left));

    // The left text is all-or-nothing and must leave at least one column
    // available for the input.  Its caller-provided width participates in
    // wrapping even though the text itself may contain terminal escapes.
    uint16_t left_text_width = 0;
    // REVIEW: should message be displayed regardless of left offset?
    if (!left)
    {
        if ((m_message_text.length() && m_message_text.width() < max_size.x) ||
            (m_left_text.length() && m_left_text.width() < max_size.x))
        {
            if (m_message_text.length())
            {
                cstring tmp_text;
                m_colors->append_color(tmp_text, color_element::base, color_element::message);
                tmp_text.append(m_message_text.c_str());
                tmp.m_left_text.set(std::move(tmp_text), m_message_text.width());
            }
            else
            {
                tmp.m_left_text = m_left_text;
            }
            left_text_width = tmp.m_left_text.width();
        }
    }

    // Calculate row boundaries without allocating display_line strings.
    // Special cases:  (1) a control character can wrap between its '^' and
    // its second displayed byte, and (2) if the final row is full then an
    // extra phantom row is needed for the cursor to land in.
    std::vector<display_row_start> rows;
    rows.push_back({ left, false });
    uint16_t row_width = left_text_width;
    int32_t y_extent = max_size.y;
    wcwidth_iter scan(text.c_str() + left, text.length() - left);
    const char* const cursor_ptr = text.c_str() + pos;
    while (scan.more())
    {
        scan.next();
        const char* const p = scan.character_pointer();
        const uint32_t clen = scan.character_length();
        const textpos_t offset = textpos_t(p - text.c_str());
        const bool cursor_in_character = (p <= cursor_ptr && cursor_ptr < p + clen);

        if (scan.character_wcwidth_signed() < 0)
        {
            if (cursor_in_character)
            {
                tmp.m_cursor.x = row_width;
                tmp.m_cursor.y = int16_t(rows.size() - 1);
            }

            // Newlines in multiline mode are literal line breaks.
            if (*p == '\n' && multiline)
            {
                rows.push_back({ textpos_t(offset + clen), false, size_t(offset) >= real_text_len });
                row_width = 0;
                continue;
            }

            // Otherwise control characters take two cells:  e.g. "^X".
            assert(clen == 1);
            if (row_width + 1 > uint16_t(max_size.x))
            {
                if (!multiline)
                    break;
                rows.push_back({ offset, false, size_t(offset) >= real_text_len });
                row_width = 0;
            }
            ++row_width;
            if (row_width + 1 > uint16_t(max_size.x))
            {
                if (!multiline)
                    break;
                rows.push_back({ offset, true, size_t(offset) >= real_text_len });
                row_width = 0;
            }
            ++row_width;
        }
        else
        {
            const uint16_t cwidth = scan.character_wcwidth_twoctrl();
            if (row_width + cwidth > uint16_t(max_size.x))
            {
                if (!multiline)
                    break;
                rows.push_back({ offset, false, size_t(offset) >= real_text_len });
                row_width = 0;
            }
            if (cursor_in_character)
            {
                tmp.m_cursor.x = row_width;
                tmp.m_cursor.y = int16_t(rows.size() - 1);
            }
            row_width += cwidth;
        }
    }

    // Determine cursor position.
    if (tmp.m_cursor.x < 0)
    {
        tmp.m_cursor.x = row_width;
        tmp.m_cursor.y = int16_t(rows.size() - 1);
    }
    if (!multiline && left && m_style->horiz_scroll_markers)
    {
        // The leading scroll marker replaces a whole grapheme, so account
        // for the difference between their displayed widths.
        wcwidth_iter iter(text.c_str() + left, text.length() - left);
        iter.next();
        const int16_t replaced_width = iter.character_wcwidth_twoctrl();
        tmp.m_cursor.x = get_horiz_scrolled_width(tmp.m_cursor.x, replaced_width);
    }
    if (tmp.m_cursor.x >= max_size.x)
    {
        assert(multiline);
        tmp.m_cursor.x = 0;
        ++tmp.m_cursor.y;
    }
    assert(implies(multiline, tmp.m_cursor.y >= 0));
    assert(implies(!multiline, tmp.m_cursor.y == 0));

    // In multiline mode, if the last line takes up the full width, then
    // there's a phantom blank line at the end.
    if (multiline && row_width == uint32_t(max_size.x))
    {
        rows.push_back({ textpos_t(text.length()), false, text.length() > real_text_len });
        row_width = 0;
        if (m_layout->variable_height && !m_style->border && m_additional_lines.empty())
            tmp.m_phantom_last_row = true;
    }
    assert(implies(!multiline, rows.size() == 1));

    // Usage text replaces ordinary right text and belongs to the final
    // logical input row.  If it cannot fit there, use one more input row
    // when the configured maximum height permits it.
    int32_t usage_text_row = -1;
    if (m_usage_text.length())
    {
        if (row_width + c_right_text_padding + m_usage_text.width() <= max_size.x)
        {
            usage_text_row = int32_t(rows.size() - 1);
            tmp.m_phantom_last_row = false;
        }
        else if (multiline &&
                 c_right_text_padding + m_usage_text.width() <= max_size.x &&
                 int32_t(rows.size()) < max_size.y)
        {
            rows.push_back({ textpos_t(text.length()), false, true });
            row_width = 0;
            usage_text_row = int32_t(rows.size() - 1);
            tmp.m_phantom_last_row = false;
        }
    }

    // Determine the height.
    const int32_t total_rows = int32_t(rows.size());
    if (m_layout->variable_height && y_extent > total_rows)
        y_extent = total_rows;
    assert(y_extent > 0);

    // Calculate the visible range before constructing its display lines.
    const int32_t min_top = max<int32_t>(tmp.m_cursor.y - (y_extent - 1), 0);
    const int32_t max_top = min<int32_t>(tmp.m_cursor.y, max<int32_t>(total_rows - y_extent, 0));
    tmp.m_top = clamp<int32_t>(m_top, min_top, max_top);

    // Lambda for building a display line.
    char pending = 0;
    bool expanding = false;
    auto build_row = [&](size_t index)
    {
        const display_row_start& start = rows[index];
        auto line = std::make_unique<display_line>(m_origin.x);
        wcwidth_iter iter(text.c_str() + start.offset, text.length() - start.offset);
        const char* face = faces.c_str() + start.offset;

        if (index == 0)
            line->m_x2 += left_text_width;

        if (start.pending)
        {
            iter.next();
            assert(iter.character_length() == 1);
            const char c = *iter.character_pointer();
            assert(uint8_t(c) < ' ' || uint8_t(c) == 0x7F);
            const char ctrl = (uint8_t(c) < ' ') ? c + '@' : '?';
            line->append(&ctrl, 1, 1, *face);
            face += iter.character_length();
        }

        if (left && m_style->horiz_scroll_markers)
        {
            // Skip the grapheme that the scroller replaces.
            iter.next();
            face += iter.character_length();

            // Append the scroller.
            for (uint16_t num = c_horz_scroll_indicator_chars; num--;)
                line->append("<", 1, 1, FACE_SCROLLER);
        }

        bool short_circuited = false;
        while (iter.more())
        {
            const char32_t c = iter.next();
            const char* p = iter.character_pointer();
            uint32_t clen = iter.character_length();
            uint32_t cwidth = iter.character_wcwidth_twoctrl();

            if (iter.character_wcwidth_signed() < 0)
            {
                if (*p == '\n' && multiline)
                {
                    face += clen;
                    break;
                }
                else
                {
                    assert(uint8_t(*p) < ' ' || uint8_t(*p) == 0x7F);
                    pending = (uint8_t(*p) < ' ') ? *p + '@' : '?';
                    expanding = true;
                    p = "^";
                    assert(clen == 1);
                    cwidth = 1;
                }
            }

again:
            if (!multiline)
            {
                if (line->width() + cwidth > uint32_t(max_size_omit_scroll_markers.x) &&
                    !(!expanding &&
                    !iter.more() &&
                    line->width() + cwidth <= uint32_t(max_size_omit_scroll_markers.x + c_horz_scroll_indicator_chars)))
                {
                    short_circuited = true;
                    break;
                }
            }
            else if (line->width() + cwidth > uint32_t(max_size.x))
            {
                break;
            }

            if (c == 0xfffd)
                line->append(c_replacement_character, c_replacement_character_length, cwidth, *face);
            else
                line->append(p, clen, cwidth, *face);

            if (expanding)
            {
                p = &pending;
                assert(clen == 1);
                assert(cwidth == 1);
                expanding = false;
                goto again;
            }

            face += clen;
        }

        if (!multiline) // Is inside the lambda because of short_circuited.
        {
            // Add horizontal scroll marker if needed.
            assert(tmp.m_lines.size() == 0);
            if (short_circuited || iter.more())
            {
                assert(!multiline);
                assert(int32_t(line->width()) < max_size.x);
                while (int32_t(line->width() + c_horz_scroll_indicator_chars) < max_size.x)
                    line->append(" ", 1, 1, FACE_DEFAULT);
                for (uint16_t num = c_horz_scroll_indicator_chars; num--;)
                    line->append(">", 1, 1, FACE_SCROLLER);
            }
        }
        return line;
    };

    // Scroll vertically when cursor is on a multiline scroll marker.
    if (y_extent < total_rows)
    {
        if (tmp.m_top == tmp.m_cursor.y)
        {
            if (tmp.m_top > 0 && tmp.m_cursor.x < c_horz_scroll_indicator_chars)
                --tmp.m_top;
        }
        else if (tmp.m_top + y_extent - 1 == tmp.m_cursor.y &&
                 tmp.m_top + y_extent < total_rows)
        {
            // Must build a row to check precisely where the scroller is in
            // the row (it may not be at the very end, depending on grapheme
            // boundaries).
            auto bottom = build_row(tmp.m_top + y_extent - 1);
            bottom->calculate_multiline_scroll_marker(max_size.x);
            if (bottom->m_trail_scroller_width_displaced &&
                tmp.m_cursor.x >= bottom->width() - bottom->m_trail_scroller_width_displaced)
                ++tmp.m_top;
        }
    }
    else
    {
        tmp.m_top = 0;
    }

    // The left text belongs only to the first logical display line.  Its
    // width still influenced that line's wrapping when it is scrolled away.
    if (tmp.m_top != 0)
        tmp.m_left_text.clear();

    // Build only the rows that will be visible.
    const int32_t end = min<int32_t>(tmp.m_top + y_extent, total_rows);
    if (end < total_rows)
        tmp.m_phantom_last_row = false;
    for (int32_t i = tmp.m_top; i < end; ++i)
    {
        tmp.m_lines.emplace_back(build_row(i));
        tmp.m_rows.emplace_back(rows[i]);
    }
    // Adjust the cursor to be relative to the origin.
    tmp.m_cursor.y -= tmp.m_top;

    // Apply scroll markers.
    tmp.apply_scroll_markers(max_size.x, y_extent, total_rows);

    // Record the effective right-aligned text in the built display snapshot.
    // Usage text supersedes ordinary right text and is attached only when its
    // target logical row is visible.  Without usage text, place right text on
    // the first displayed row when it fits.
    if (m_usage_text.length())
    {
        if (usage_text_row >= tmp.m_top && usage_text_row < end)
        {
            tmp.m_right_text = m_usage_text;
            tmp.m_right_text_row = usage_text_row - tmp.m_top;
        }
    }
    else if (!tmp.m_lines.empty() && m_right_text.width() &&
             tmp.m_lines.front()->width() + c_right_text_padding + m_right_text.width() <= max_size.x)
    {
        tmp.m_right_text = m_right_text;
        tmp.m_right_text_row = 0;
    }

    // Handle fixed height mode.
    while (int32_t(tmp.m_lines.size()) < y_extent)
    {
        tmp.m_lines.emplace_back(std::move(std::make_unique<display_line>(m_origin.x)));
        tmp.m_rows.push_back({ c_padding_row_offset, false });
    }

    assert(implies(!multiline, !tmp.m_top));
    assert(implies(!multiline, y_extent == 1));
    assert(implies(!multiline, tmp.m_lines.size() == 1));

    tmp.m_extent.x += max_size.x;
    tmp.m_extent.y += y_extent;
    tmp.m_additional_lines = m_additional_lines;
    tmp.m_extent.y += int32_t(tmp.m_additional_lines.size());

    assert(tmp.m_cursor.y >= 0);
    assert(size_t(tmp.m_cursor.y) < tmp.m_lines.size());

    ++s_build.action;
    s_build.row_total += tmp.m_lines.size();

    out = std::move(tmp);
    return true;
}

void display_manager::append_border(coord extent)
{
    assert(is_initialized());
    assert(m_style);

    const border_definition& b = *m_style->border;
#if 0
    assert(implies(m_style->border, b.is_valid()));
#endif

    const uint16_t b_left_width = b.get_left_width();
    const uint16_t b_right_width = b.get_right_width();
    const uint16_t extra_border_width = b_left_width + b_right_width;
    const uint16_t extra_border_height = b.has_top() + b.has_bottom();
    const coord max_size = get_effective_max_size();
    if (max_size.x <= 0 || max_size.y <= 0)
        return;
    assert(extent.x == max_size.x + extra_border_width);
    assert(max_size.y >= extent.y - extra_border_height);

    auto finish_border_wrap = [&](int32_t row)
    {
#ifdef _WIN32
        if (b_right_width && m_origin.x + extent.x - 1 == m_term_size.x)
        {
            coord cursor = { extent.x, row };
            detect_pending_wrap(cursor);
            finish_pending_wrap(cursor);
            output_color(m_colors->get_color(color_element::border));
            return true;
        }
#endif
        return false;
    };

    bool wrapped = false;
    int32_t row = 0;
    output_color(m_colors->get_color(color_element::border));

    if (b.has_top())
    {
        output(term_col(m_origin.x));
        if (b.top_left)
            output(b.top_left);
        const int16_t top_width = b.get_top_width();
        for (int32_t i = extent.x - (b.get_top_left_width() + b.get_top_right_width()); i - top_width >= 0; i -= top_width)
            output(b.top);
        if (b.top_right)
            output(b.top_right);
        wrapped = finish_border_wrap(row);
    }

    bool first = true;
    for (uint32_t i = extent.y - extra_border_height; i--; first = false)
    {
        // Finishing a pending wrap already advances to the next row.
        if (!wrapped)
            output("\r\n");
        ++row;
        if (b_left_width)
        {
            output(term_col(m_origin.x));
            output((!b.left_2 || first) ? b.left : b.left_2);
        }
        if (b_right_width)
        {
            output(term_col(m_origin.x + extent.x - b_right_width));
            output((!b.right_2 || first) ? b.right : b.right_2);
        }
        wrapped = finish_border_wrap(row);
    }

    if (b.has_bottom())
    {
        if (!wrapped)
            output("\r\n");
        ++row;
        output(term_col(m_origin.x));
        if (b.bottom_left)
            output(b.bottom_left);
        const int16_t bottom_width = b.get_bottom_width();
        for (int32_t i = extent.x - (b.get_bottom_left_width() + b.get_bottom_right_width()); i - bottom_width >= 0; i -= bottom_width)
            output(b.bottom);
        if (b.bottom_right)
            output(b.bottom_right);
        wrapped = finish_border_wrap(row);
    }

    if (extent.y - 1 + wrapped > 0)
        output(term_move_up(extent.y - 1 + wrapped));
}

void display_manager::output(const char* s, size_t len)
{
#ifdef _WIN32
    assert(!m_pending_wrap);
    m_pending_wrap = false;
#endif

    m_accumulator.append(s, len);
    maybe_flush();
}

void display_manager::outputf(const char* format, ...)
{
#ifdef _WIN32
    assert(!m_pending_wrap);
    m_pending_wrap = false;
#endif

    va_list args;
    va_start(args, format);

    m_accumulator.printfv(format, args);
    maybe_flush();

    va_end(args);
}

void display_manager::output_color(const char* sgr_params)
{
    // Printing VT color codes does not affect wrapping.
    m_accumulator.append_color(sgr_params);
    maybe_flush();
}

void display_manager::output_spaces(size_t n)
{
#ifdef _WIN32
    assert(!m_pending_wrap);
    m_pending_wrap = false;
#endif

    m_accumulator.append_spaces(n);
    maybe_flush();
}

void display_manager::maybe_flush()
{
    if (m_coalesce_output)
        return;

    do_flush();
}

bool display_manager::is_initialized() const
{
    return m_layout && m_buffer;
}

void display_manager::do_flush()
{
    term_out(m_accumulator.c_str(), m_accumulator.length());
    m_accumulator.clear();
}

#ifdef _WIN32
static HANDLE is_horizpos_workaround_needed()
{
    if (is_test_harness())
        return nullptr;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return nullptr;
    if (csbi.srWindow.Left == 0 && csbi.srWindow.Right == csbi.dwSize.X - 1)
        return nullptr;
    return h;
}

void display_manager::init_horizpos_workaround()
{
    assert(is_initialized());

    m_horizpos_workaround = is_horizpos_workaround_needed();
}

void display_manager::detect_pending_wrap(coord& cursor)
{
    assert(is_initialized());

    // cursor.x identifies the next output column, so it advances one column
    // past the terminal width after the rightmost cell has been printed.
    if (m_origin.x + cursor.x > m_term_size.x)
    {
        cursor.x = 1 - m_origin.x;
        ++cursor.y;
        m_pending_wrap = true;
    }
    else
    {
        m_pending_wrap = false;
    }
}

void display_manager::finish_pending_wrap(coord& cursor)
{
    assert(is_initialized());

    // This finishes a pending wrap using a technique that works equally well
    // on both Win 8.1 and Win 10.
    assert(m_pending_wrap);
    assert(m_pending_wrap_display);
    assert(cursor.x == 1 - m_origin.x);

    if (!m_pending_wrap)
        return;

    // Code below uses output() which participates in the pending wrap logic,
    // so m_pending_wrap must be cleared before proceeding.
    m_pending_wrap = false;

    uint32_t bytes = 0;

    // If there's a display_line, then re-print its first character to force
    // wrapping.  Otherwise, print a placeholder.
    const size_t index = cursor.y - m_pending_wrap_display->m_inner_offset.y;
    assert(index >= 0);
    if (index < m_pending_wrap_display->m_lines.size())
    {
        const display_line& d = *m_pending_wrap_display->m_lines[index];

        if (d.m_x1 == 1)
        {
            wcwidth_iter iter(d.m_text.c_str(), d.m_text.length());
            uint32_t cols = 0;
            while (iter.next())
            {
                const int32_t wc = iter.character_wcwidth_onectrl();
                cols += wc;
                if (wc)
                    break;
            }

            bytes = uint32_t(iter.get_pointer() - d.m_text.c_str());
            if (bytes)
            {
                coord dummy;
                print_text_with_faces(dummy, d.m_text.c_str(), d.m_faces.c_str(), bytes);
                output("\r", 1);
            }
        }
    }

    if (!bytes)
    {
        if (m_autowrap_bug)
        {
            output("\r");
        }
        else
        {
            // If there's no display_line or it's empty, print a space to
            // force wrapping and a backspace to move the cursor to the
            // beginning of the line with the fewest possible side effects
            // (which potentially matters during terminal resize, which is
            // asynchronous with respect to the console application).
            output("\x1b[m \x08", 5);
        }
    }
}
#endif // _WIN32

#ifdef _WIN32
void display_manager::clr_to_eol(int32_t spaces)
{
    if (spaces <= 0)
        return;

    if (s_can_use_clreol)
        output(term_erase_to_eol());
    else
        output_spaces(spaces);
}
#else
void display_manager::clr_to_eol(int32_t /*spaces*/)
{
    output(term_erase_to_eol());
}
#endif

} // namespace tib
