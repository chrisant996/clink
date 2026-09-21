// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"
#include "tib_buffer.h"
#include "tib_colors.h"
#include "tib_grapheme.h"
#include "tib_terminal.h"
#include "wcwidth.h"
#include <memory>
#include <vector>
#include <map>

class wcwidth_iter;

namespace tib {

extern bool g_coalesce_output;
extern bool g_show_hide_cursor;

struct border_definition
{
#if 0
    bool                is_valid() const;
#endif

    // FUTURE: Allow callback functions for header and footer, or for building
    // a custom strings for the top and bottom borders?

    bool                has_top() const { return this && top && *top; }
    bool                has_bottom() const { return this && bottom && *bottom; }
    bool                has_left() const { return this && left && *left; }
    bool                has_right() const { return this && right && *right; }

    // These allow a border_definition to include embedded ANSI escape
    // sequences without requiring terminal-input-box to include an ECMA48
    // compliant escape sequence parser.
    int8_t              get_top_left_width() const { return !this ? 0 : get_width(top_left, top_left_width); }
    int8_t              get_top_width() const { return !this ? 0 : get_width(top, top_width); }
    int8_t              get_top_right_width() const { return !this ? 0 : get_width(top_right, top_right_width); }
    int8_t              get_left_width() const { return !this ? 0 : get_width(left, left_width); }
    int8_t              get_right_width() const { return !this ? 0 : get_width(right, right_width); }
    int8_t              get_bottom_left_width() const { return !this ? 0 : get_width(bottom_left, bottom_left_width); }
    int8_t              get_bottom_width() const { return !this ? 0 : get_width(bottom, bottom_width); }
    int8_t              get_bottom_right_width() const { return !this ? 0 : get_width(bottom_right, bottom_right_width); }

    const char*         top_left = nullptr;
    const char*         top = nullptr;
    const char*         top_right = nullptr;
    const char*         left = nullptr;
    const char*         right = nullptr;
    const char*         bottom_left = nullptr;
    const char*         bottom = nullptr;
    const char*         bottom_right = nullptr;

    int8_t              top_left_width = -1;
    int8_t              top_width = -1;
    int8_t              top_right_width = -1;
    int8_t              left_width = -1;
    int8_t              right_width = -1;
    int8_t              bottom_left_width = -1;
    int8_t              bottom_width = -1;
    int8_t              bottom_right_width = -1;

    const char*         left_2 = nullptr;
    const char*         right_2 = nullptr;

private:
    int8_t              get_width(const char* s, int8_t width) const;
};

extern const border_definition c_light_border;

constexpr char FACE_DEFAULT     = 0x20;
constexpr char FACE_SELECTION   = 0x1f;
constexpr char FACE_MARK        = 0x1e;
constexpr char FACE_SCROLLER    = 0x1d;
constexpr char FACE_SUGGESTION  = 0x1c;
constexpr char FACE_EMPTY       = 0;
struct editor_callbacks;
typedef std::map<char, cstring> face_definitions;

// REVIEW: allow runtime configuration of the horz scroll indicator width?
constexpr uint16_t c_horz_scroll_indicator_chars = 1;
static_assert(c_horz_scroll_indicator_chars >= 1);
static_assert(c_horz_scroll_indicator_chars <= 3);

struct layout_info
{
    uint16_t            max_width = int16_max;
    uint16_t            max_height = 1;
    bool                variable_height = false;
};

struct style_info
{
    bool                horiz_scroll_markers = true;
    const border_definition* border = nullptr;
    char                empty_face = FACE_EMPTY;
};

class text_and_width
{
public:
    void                clear();
    bool                set(const char* text, uint16_t width);
    bool                set(cstring&& text, uint16_t width);
    bool                operator==(const text_and_width& other) const;
    const char*         c_str() const { return m_text.c_str(); }
    size_t              length() const { return m_text.length(); }
    uint16_t            width() const { return m_width; }

private:
    cstring             m_text;
    uint16_t            m_width = 0;
};

struct additional_display_line
{
    bool                operator==(const additional_display_line& other) const noexcept;

    cstring             text;
    uint16_t            width = 0;
    bool                bounded = true;
};

struct display_line
{
                        ~display_line() = default;
                        display_line(uint16_t x1);
    void                append(const char* p, uint32_t len, uint32_t width, char face);
    uint16_t            width() const { return m_x2 - m_x1; }
    void                calculate_multiline_scroll_marker(uint16_t max_width);

    cstring             m_text;
    cstring             m_faces;
    uint16_t            m_x1 = 0;       // First column in the line (1-based, inclusive).
    uint16_t            m_x2 = 0;       // Last column in the line (1-based, EXCLUSIVE).
#if 0
    uint16_t            m_lead = 0;     // Number of leading columns (e.g. wrapped part of ^X).
    uint16_t            m_trail = 0;    // Number of trailing columns of spaces past m_lastcol.
#endif
    uint8_t             m_trail_scroller_width_displaced = 0;
    uint8_t             m_trail_scroller_len_displayed = 0;
};

struct display_row_start
{
    textpos_t           offset;
    bool                pending;
    bool                virtual_text = false;
};

enum class screen_scroll_direction
{
    none,
    up,
    down,
    left,
    right,
};

struct screen_scroll_info
{
    screen_scroll_direction direction = screen_scroll_direction::none;
    int32_t             cursor_column = 0;
    int32_t             vertical_row_delta = 0;
};

struct display_lines
{
    void                clear();
    void                apply_scroll_markers(int16_t x_extent, int32_t y_extent, int32_t total_rows);

    int32_t             m_top = 0;
    textpos_t           m_pos = 0;
    textpos_t           m_anchor = 0;
    textpos_t           m_mark = -1;
    textpos_t           m_left = 0;
    uint32_t            m_change_counter = 0;

    std::vector<std::unique_ptr<display_line>> m_lines;
    std::vector<display_row_start> m_rows;
    text_and_width      m_left_text;
    text_and_width      m_right_text;
    int32_t             m_right_text_row = -1;
    std::vector<additional_display_line> m_additional_lines;
    coord               m_cursor = { -1, -1 };  // Offset from m_inner_offset.

    coord               m_inner_offset = { 0, 0 };
    coord               m_extent = { 0, 0 };

    bool                m_phantom_last_row = false;
    bool                m_erase = false;
};

class input_buffer;

class display_manager
{
    friend class preserve_window_horiz_scroll_position;

public:
                        ~display_manager() = default;
                        display_manager();

    void                init_layout(const layout_info* layout);
    void                init_buffer(const input_buffer* buffer);
    void                init_style(const style_info* style);
    void                init_faces(const face_definitions* face_defs);
    void                init_callbacks(editor_callbacks* callbacks);

    coord               get_origin() const { return m_origin; }
    void                set_origin(int32_t x=-1, int32_t y=-1);

    std::shared_ptr<const color_table> get_color_table() const;
    void                set_color_table(std::shared_ptr<const color_table> colors);

    void                set_left_text(const char* left, uint16_t width);
    void                set_right_text(const char* right, uint16_t width);
    void                set_message_text(const char* message, uint16_t width);
    void                set_suggestion_text(const char* suggestion, size_t len);
    void                set_usage_text(const char* usage, uint16_t width);
    void                set_additional_lines(const std::vector<additional_display_line>& lines);
    void                clear_additional_lines();

    coord               get_effective_max_size(bool omit_scroll_markers=false);
    coord               get_relative_cursor() const { return m_relative_cursor; }
    coord               get_extent() const;
    coord               get_inner_extent() const;

    textpos_t           get_left() const { return m_left; }
    uint32_t            get_top() const { return m_top; }
    void                set_scroll_offsets(textpos_t left, uint32_t top);
    void                clear_scroll_offsets();
    bool                scroll_horizontally(int32_t columns, int32_t cursor_column, selection_state& selection, bool exclude_auto_scroll=true);
    bool                move_caret_vertically(int32_t rows, int32_t cursor_column, selection_state& selection, bool select=false);
    bool                get_pos_from_screen(uint32_t x, uint32_t y, textpos_t& pos, screen_scroll_info* scroll=nullptr);
    bool                set_caret_from_screen(uint32_t x, uint32_t y, selection_state& selection, uint32_t drag_scroll_chars=0, bool word_drag=false);
    void                suppress_auto_horizontal_scroll(const selection_state& selection);

    void                begin_display();
    void                invalidate() { m_invalidated = true; }
    void                invalidate_border() { m_border_dirty = true; }
    bool                display();
    void                force_redisplay();
    void                move_to_end_of_display();
    void                move_to_caret_position();
    void                erase_display();
    void                end_display_lf();

private:
    void                move_to_row(coord& cursor, uint16_t y, uint16_t inner_offset);
    void                move_to_column(coord& cursor, uint16_t x, uint16_t inner_offset);
    void                print_text_with_faces(coord& cursor, const char* text, const char* faces, size_t len);
    const char*         get_face_def(char face) const;
    bool                try_update_caret_only();
    bool                display_internal(display_lines& lines);
    void                ensure_left();
    bool                build(display_lines& out);
    void                append_border(coord extent);

    void                output(const char* s, size_t len=-1);
    void                outputf(const char* format, ...);
    void                output_color(const char* color);
    void                output_spaces(size_t n);
    void                maybe_flush();
    void                do_flush();

    bool                is_initialized() const;

#ifdef _WIN32
    void                init_horizpos_workaround();
    void                detect_pending_wrap(coord& cursor);
    void                finish_pending_wrap(coord& cursor);
#endif
    void                clr_to_eol(int32_t spaces);

private:
    const layout_info*  m_layout = nullptr;         // Borrowed.
    const input_buffer* m_buffer = nullptr;         // Borrowed.
    const style_info*   m_style = nullptr;          // Borrowed.
    const face_definitions* m_face_defs = nullptr;  // Borrowed.
    editor_callbacks*   m_callbacks = nullptr;      // Borrowed.
    coord               m_origin = { -1, -1 };
    coord               m_term_size;
    std::shared_ptr<const color_table> m_colors;
    display_lines       m_displayed;
    text_and_width      m_left_text;
    text_and_width      m_right_text;
    text_and_width      m_message_text;
    cstring             m_suggestion_text;
    text_and_width      m_usage_text;
    std::vector<additional_display_line> m_additional_lines;
    uint32_t            m_top = 0;                  // Vertical scroll top.
    textpos_t           m_left = 0;                 // Horizontal scroll left.
    bool                m_display_ended = false;
    bool                m_border_dirty = false;
    bool                m_invalidated = false;
    bool                m_force_redisplay = false;
    bool                m_hwheel_exclusion = false;
    textpos_t           m_hwheel_exclusion_left = 0;
    textpos_t           m_hwheel_exclusion_caret = 0;
    uint32_t            m_hwheel_exclusion_change_counter = 0;
    coord               m_relative_cursor = { -1, -1 };

    cstring             m_accumulator;
    bool                m_coalesce_output = false;

#ifdef _WIN32
    HANDLE              m_horizpos_workaround = 0;
    const bool          m_autowrap_bug;
    bool                m_pending_wrap = false;
    const display_lines* m_pending_wrap_display = nullptr;
#endif

    std::vector<grapheme_info> m_tmp_graphemes;
};

void show_display_manager_statistics(bool show);

#ifdef _WIN32
bool is_autowrap_bug_present();
#endif

} // namespace tib
