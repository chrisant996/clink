// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"
#include "tib_bindings.h"
#include "tib_buffer.h"
#include "tib_colors.h"
#include "tib_display.h"
#include <map>
#include <vector>

struct grapheme_info;

namespace tib {

extern bool g_optimize_self_insert;

// Returning negative from an editor_command_func_t signals that the binding
// was not handled and implies permission for something else to choose to
// handle the binding.
typedef int32_t (*editor_command_func_t)(editor_context& ctx, int32_t key, const char* name, const binding_params* params);

struct editor_command
{
    const char*         name;
    editor_command_func_t func;
};

textpos_t pos_mover(const char* s, const size_t _len, textpos_t& pos, const bool forward, const uint8_t word);

struct editor_callbacks
{
    virtual void        provide_faces(const input_buffer& buffer, cstring& faces) {}
};

struct editor_quirks
{
    bool                bash_digit_argument = false;    // Bash makes it impossible to enter -2 or -2345:  Alt-Minus Alt-2 becomes -12.
};

struct undo_entry
{
                        undo_entry() = default;
                        ~undo_entry();
    void                link_at_tail(undo_entry*& head, undo_entry*& tail);
    void                unlink(undo_entry*& head, undo_entry*& tail);

    cstring             m_text;
    selection_state     m_sel_before;
    selection_state     m_sel_after;
    textpos_t           m_left = 0;
    int32_t             m_top = 0;

    undo_entry*         m_prev = nullptr;
    undo_entry*         m_next = nullptr;
};

class editor_context
    : public dispatcher_target // enable_shared_from_this
    , public input_buffer
{
public:
                        ~editor_context();
                        editor_context();

    void                initialize(const char* text=nullptr, size_t len=c_auto_length);
    void                reset_state() noexcept;
    bool                done() const noexcept { return m_done; }
    void                set_done() noexcept { m_done = true; }

    void                set_max_length(uint32_t m) { m_max_length = static_cast<textpos_t>(min<uint32_t>(m, int16_max)); }
    void                set_max_width(uint16_t m) { m_layout.max_width = static_cast<textpos_t>(min<uint16_t>(m, int16_max)); }
    void                set_max_height(uint16_t m) { m_layout.max_height = static_cast<textpos_t>(min<uint16_t>(m, int16_max)); }
    void                set_variable_height(bool v) { m_layout.variable_height = v; }
    const border_definition* get_border() const { return m_style.border; }
    void                set_border(const border_definition* border);
    void                set_horiz_scroll_markers(bool show) { m_style.horiz_scroll_markers = show; }
#if 0
    void                set_history(std::vector<StrW>* history);
#endif
    coord               get_origin() const { return m_display.get_origin(); }
    void                set_origin(int16_t x=-1, int16_t y=-1) { m_display.set_origin(x, y); }
    coord               get_relative_cursor() const { return m_display.get_relative_cursor(); }
    coord               get_extent() const { return m_display.get_extent(); }
    coord               get_inner_extent() const { return m_display.get_inner_extent(); }
    textpos_t           get_left() const { return m_display.get_left(); }
    uint32_t            get_top() const { return m_display.get_top(); }

    void                set_callbacks(editor_callbacks* callbacks);
    std::shared_ptr<const color_table> get_color_table() const;
    void                set_color_table(std::shared_ptr<const color_table> colors);
    void                set_face_defs(const face_definitions* face_defs);
    void                set_empty_face(char face);
    const editor_quirks& get_quirks() const noexcept { return m_quirks; }
    void                set_quirks(const editor_quirks& quirks) noexcept { m_quirks = quirks; }

    void                set_left_text(const char* left, uint16_t width);
    void                set_right_text(const char* right, uint16_t width);
    void                set_suggestion_text(const char* suggestion, size_t len=c_auto_length);
    void                set_usage_text(const char* usage, uint16_t width);
    void                set_additional_lines(const std::vector<additional_display_line>& lines);
    void                clear_additional_lines();

    void                begin_display();
    void                invalidate();
    void                invalidate_border();
    void                display();
    void                force_redisplay();
    void                move_to_caret_position();
    void                move_to_end_of_display();
    void                erase_display();
    void                end_display_lf();

    void                begin_of_input(bool select=false);
    void                end_of_input(bool select=false);
    bool                move_left(uint8_t word=0, bool select=false);
    bool                move_right(uint8_t word=0, bool select=false);
    bool                backspace(uint8_t word=0);
    bool                del(uint8_t word=0);
    void                del_line();
    bool                transpose(uint8_t word=0);

    void                clear_selection();
    bool                set_caret(textpos_t caret);
    bool                set_selection(textpos_t anchor, textpos_t caret);
    bool                extend_selection(textpos_t pos, uint8_t word=0);
    void                get_range_at_click(textpos_t pos, uint8_t word, textpos_t& begin, textpos_t& end);
    bool                select_word(bool bigword=false);
    void                reset_word_anchor() { m_selection.reset_word_anchor(); }

    textpos_t           get_mark() const { return m_selection.get_mark(); }
    bool                set_mark(textpos_t mark);
    bool                is_mark_active() const { return m_selection.is_mark_active(); }
    bool                set_mark_active(bool active=true);
    void                clear_auto_deactivate_mark();
    bool                exchange_caret_and_mark();

#if _WIN32
    bool                copy_to_clipboard();
    bool                cut_to_clipboard();
    bool                paste_from_clipboard();
#endif
    bool                get_overwrite_mode() const noexcept { return m_overwrite_mode; }
    void                set_overwrite_mode(bool overwrite) noexcept;
    void                toggle_overwrite_mode() noexcept { set_overwrite_mode(!m_overwrite_mode); }

    const char*         get_last_command() const noexcept { return m_last_command.c_str(); }
    void                set_last_command(const char* name);
    const char*         get_named_value(const char* name) const;
    int32_t             get_named_value_int(const char* name, int32_t def=0) const;
    void                set_named_value(const char* name, const char* value);
    void                set_named_value_int(const char* name, int32_t value);
    void                clear_named_value(const char* name);

    void                set_auto_clear_numeric_argument(bool clear=true);
    void                clear_numeric_argument();
    bool                has_numeric_argument() const;
    int32_t             get_argument_sign() const;
    void                set_argument_sign(int32_t sign);
    void                invert_argument_sign();
    int32_t             get_numeric_argument() const;
    void                set_numeric_argument(int32_t value);
    bool                numeric_digit(int32_t key);
    bool                universal_argument();

    bool                scroll_horizontally(int32_t columns, int32_t cursor_column, bool exclude_auto_scroll=true);
    bool                move_caret_vertically(int32_t rows, int32_t cursor_column, bool select=false);
    bool                get_pos_from_screen(uint32_t x, uint32_t y, textpos_t& pos, screen_scroll_info* scroll=nullptr);
    bool                set_caret_from_screen(uint32_t x, uint32_t y, uint32_t drag_scroll_chars=0, bool word_drag=false);
    void                suppress_auto_horizontal_scroll();

#if 0
    void                replace_from_history(const cstring& text, bool keep_undo);
#endif
    void                insert_char(char c, bool overwrite=false);
    void                insert_text(const char* text, size_t len=c_auto_length, bool overwrite=false);
    void                remove_text(textpos_t begin, textpos_t end);
    bool                elide_selected_text();

    void                clear_undo() { init_undo(); }
    void                begin_undo_group();
    void                end_undo_group();
    bool                undo();
    bool                undo_all();
    bool                redo();

    void                transfer_text(cstring& out);

#ifdef DEBUG
    void                dump_undo_stack();
#endif

    static void         ensure_commands();
    static void         register_command(const char* name, editor_command_func_t func);
    static const std::vector<editor_command>& get_registered_commands();
    static editor_command_func_t lookup_command(const char* name);

                        // Methods on the tib::dispatcher_target interface.
    int32_t             dispatch(const cstring& sequence, int32_t key, const binding_target* binding, const binding_params* params) noexcept override;
    bool                on_binding_miss(const cstring& sequence, int32_t key) noexcept override;

protected:
    bool                get_allow_optimized_self_insert() const { return m_allow_optimized_self_insert; }
    void                set_allow_optimized_self_insert(bool allow) { m_allow_optimized_self_insert = allow; }

private:
    void                init_undo();
    void                clear_undo_internal();
    void                unlink_endo_entry(undo_entry* p);
    void                inc_change_counter();
    void                begin_undo_group(bool merge);
    void                insert_raw_char(char c);
    void                clear_overwrite_input();
    void                apply_message_text();
    void                apply_override_bindings();

    static void         ensure_commands_sorted();

private:
    struct cstring_less
    {
        using is_transparent = void;
        bool operator()(const cstring& lhs, const cstring& rhs) const { return strcmp(lhs.c_str(), rhs.c_str()) < 0; }
        bool operator()(const cstring& lhs, const char* rhs) const { return strcmp(lhs.c_str(), rhs) < 0; }
        bool operator()(const char* lhs, const cstring& rhs) const { return strcmp(lhs, rhs.c_str()) < 0; }
    };

    // NOTE:  Content and selection are contained in the base class.

    // Configuration.
    editor_callbacks*   m_callbacks = nullptr;      // Borrowed.
    layout_info         m_layout;   // REVIEW: does tib_context actually need access to this?
    style_info          m_style;    // REVIEW: does tib_context actually need access to this?
    editor_quirks       m_quirks;
    uint32_t            m_max_length = int16_max;

    // State.
    uint16_t            m_terminal_row = 0;
#if 0
    MouseHelper         m_mouse_helper;
#endif
    bool                m_done = false;
    bool                m_can_drag = false;
    bool                m_auto_deactivate_mark = true;
    bool                m_allow_optimized_self_insert = true;
    bool                m_overwrite_mode = false;
    bool                m_replaying_overwrite_input = false;
    cstring             m_overwrite_input;
    cstring             m_overwrite_input_original_text;
    selection_state     m_overwrite_input_original_selection;
    uint32_t            m_overwrite_input_change_counter = 0;
    uint32_t            m_overwrite_input_navigation_counter = 0;
    cstring             m_last_command;
    std::map<cstring, cstring, cstring_less> m_named_values;

    // Numeric argument.
    uint8_t             m_numflags = 0;
    int8_t              m_sign_numeric_argument = 0;
    int32_t             m_numeric_argument = 0;
    int32_t             m_quoted_insert_count = 0;

    // Display.
    display_manager     m_display;

    // Undo/redo queue.
    undo_entry*         m_undo_head = nullptr;
    undo_entry*         m_undo_tail = nullptr;
    undo_entry*         m_undo_current = nullptr;
    int16_t             m_grouping = 0;  // >0 means an undo group is in progress.
    bool                m_defer_init_undo = false;

    // History.
#if 0
    std::vector<cstring>* m_history = nullptr;
    size_t              m_history_index = 0;
    cstring             m_curr_input_history;
#endif

    // Commands.
    static std::vector<editor_command> s_commands;
    static std::vector<cstring> s_command_names;
    static bool         s_unsorted_commands;
};

} // namespace tib
