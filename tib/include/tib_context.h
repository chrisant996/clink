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
typedef int32_t (*editor_command_func_t)(editor_context& ctx, int32_t key, const char* name, const resolved_binding* resolved);

struct editor_command
{
    const char*         name;
    editor_command_func_t func;
};

typedef std::vector<editor_command> command_table;

textpos_t pos_mover(const char* s, const size_t _len, textpos_t& pos, const bool forward, const uint8_t word);

struct editor_callbacks;

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

struct undo_history
{
                        undo_history() = default;
                        ~undo_history();
                        undo_history(const undo_history&) = delete;
    undo_history&       operator=(const undo_history&) = delete;
    void                clear() noexcept;

    undo_entry*         m_head = nullptr;
    undo_entry*         m_tail = nullptr;
    undo_entry*         m_current = nullptr;
    int16_t             m_grouping = 0;
    bool                m_defer_init = false;
};

struct editing_state
{
    struct cstring_less
    {
        using is_transparent = void;
        bool operator()(const cstring& lhs, const cstring& rhs) const { return strcmp(lhs.c_str(), rhs.c_str()) < 0; }
        bool operator()(const cstring& lhs, const char* rhs) const { return strcmp(lhs.c_str(), rhs) < 0; }
        bool operator()(const char* lhs, const cstring& rhs) const { return strcmp(lhs, rhs.c_str()) < 0; }
    };

    undo_history        undo;

    text_and_width      m_message_text;

    bool                m_auto_deactivate_mark = true;

    bool                m_overwrite_mode = false;
    bool                m_replaying_overwrite_input = false;
    cstring             m_overwrite_input;
    cstring             m_overwrite_input_original_text;
    selection_state     m_overwrite_input_original_selection;
    uint32_t            m_overwrite_input_change_counter = 0;
    uint32_t            m_overwrite_input_navigation_counter = 0;

    cstring             m_last_command_name;
    editor_command_func_t m_last_command_func = nullptr;

    struct named_value
    {
        cstring         text;
        uint32_t        change_counter = 0; // Zero means unpinned.
    };
    std::map<cstring, named_value, cstring_less> m_named_values;
};

struct parameter_request
{
    text_and_width      prompt;
    cstring             initial_text;
    uint32_t            max_length = int16_max;
    // Additional tables, in priority order; later tables override earlier
    // ones and the built-in parameter bindings.  At least one non-null table
    // is required.
    std::vector<std::shared_ptr<key_table>> bindings;
    // Additional commands, invokable only while the parameter_request is
    // active (note that the commands are invokable from any key tables for as
    // long as the parameter_request is active.  At least one command is
    // required (otherwise how could a parameter be accepted?).
    std::shared_ptr<const command_table> commands;
};

class editor_context
    : public dispatcher_target // enable_shared_from_this
{
public:
                        ~editor_context();
                        editor_context();

    // Initialization and "done" status.
    void                initialize(const char* text=nullptr, size_t len=c_auto_length);
    void                reset_state() noexcept;
    bool                is_done() const noexcept { return m_done; }
    void                set_done() noexcept;
    void                transfer_text(cstring& out);

    // tib::dispatcher_target interface...
    int32_t             dispatch(const cstring& sequence, int32_t key, const binding_target* binding, const resolved_binding* resolved) noexcept override;
    void                on_binding_fallback(const binding_handle& handle_crossed) noexcept override;
    void                report_error(const char* msg) noexcept override;

    // Configuration.
    void                set_max_length(uint32_t m) { m_max_length = static_cast<textpos_t>(min<uint32_t>(m, int16_max)); }
    void                set_max_width(uint16_t m) { m_layout.max_width = static_cast<textpos_t>(min<uint16_t>(m, int16_max)); }
    void                set_max_height(uint16_t m) { m_layout.max_height = static_cast<textpos_t>(min<uint16_t>(m, int16_max)); }
    void                set_variable_height(bool v) { m_layout.variable_height = v; }
    void                set_callbacks(editor_callbacks* callbacks);
#if 0
    void                set_history(std::vector<StrW>* history);
#endif

    // Visual styling.
    const border_definition* get_border() const { return m_style.border; }
    void                set_border(const border_definition* border);
    std::shared_ptr<const color_table> get_color_table() const;
    void                set_color_table(std::shared_ptr<const color_table> colors);
    const editor_quirks& get_quirks() const noexcept { return m_quirks; }
    void                set_quirks(const editor_quirks& quirks) noexcept { m_quirks = quirks; }
    void                set_empty_face(char face);
    void                set_horiz_scroll_markers(bool show) { m_style.horiz_scroll_markers = show; }

    // Terminal metrics.
    coord               get_origin() const { return m_display.get_origin(); }
    void                set_origin(int16_t x=-1, int16_t y=-1) { m_display.set_origin(x, y); }
    coord               get_relative_cursor() const { return m_display.get_relative_cursor(); }
    coord               get_extent() const { return m_display.get_extent(); }
    coord               get_inner_extent() const { return m_display.get_inner_extent(); }
    textpos_t           get_left() const { return m_display.get_left(); }
    uint32_t            get_top() const { return m_display.get_top(); }


    void                set_left_text(const char* left, uint16_t width);
    void                set_right_text(const char* right, uint16_t width);
    void                set_suggestion_text(const char* suggestion, size_t len=c_auto_length);
    void                set_usage_text(const char* usage, uint16_t width);
    void                set_message_text(const char* message, uint16_t width);
    void                set_additional_lines(const std::vector<additional_display_line>& lines);
    void                clear_additional_lines();

    // Content.
    const cstring&      get_text() const { return m_buffer.m_text; }
    size_t              get_length() const { return m_buffer.m_text.length(); }
    uint32_t            get_change_counter() const { return m_buffer.m_change_counter; }
    void                inc_change_counter();

    // Selection.
    textpos_t           get_caret() const { return m_buffer.m_selection.get_caret(); }
    textpos_t           get_anchor() const { return m_buffer.m_selection.get_anchor(); }
    textpos_t           get_sel_begin() const { return m_buffer.m_selection.get_sel_begin(); }
    textpos_t           get_sel_end() const { return m_buffer.m_selection.get_sel_end(); }
    textpos_t           get_word_anchor_begin() const { return m_buffer.m_selection.get_word_anchor_begin(); }
    textpos_t           get_word_anchor_end() const { return m_buffer.m_selection.get_word_anchor_end(); }
    textpos_t           get_mark() const { return m_buffer.m_selection.get_mark(); }
    bool                is_mark_active() const { return m_buffer.m_selection.is_mark_active(); }
    bool                has_selection() const { return m_buffer.m_selection.has_selection(); }
    const selection_state& get_selection_state() const { return m_buffer.m_selection; }
    void                clear_selection();
    bool                set_caret(textpos_t caret);
    bool                set_selection(textpos_t anchor, textpos_t caret);
    bool                extend_selection(textpos_t pos, uint8_t word);
    void                get_range_at_click(textpos_t pos, uint8_t word, textpos_t& begin, textpos_t& end);
    bool                select_word(bool bigword=false);
    bool                set_mark(textpos_t mark);
    bool                set_mark_active(bool active=true);
    void                reset_word_anchor() { m_buffer.m_selection.reset_word_anchor(); }
    textpos_t           clamp_in_range(textpos_t pos) const;
    void                clear_auto_deactivate_mark();

    // Display.
    void                begin_display();
    void                invalidate();
    void                invalidate_border();
    bool                is_displayed() const;
    void                display();
    void                force_redisplay();
    void                move_to_origin(bool force_left_edge=false);
    void                move_to_caret_position(bool force_column=false);
    void                move_to_end_of_display(bool cr=false);
    void                erase_display();
    void                end_display_lf();

    // Content modification.
#if 0
    void                replace_from_history(const cstring& text, bool keep_undo);
#endif
    void                insert_char(char c, bool overwrite=false);
    void                insert_text(const char* text, size_t len=c_auto_length, bool overwrite=false);
    void                remove_text(textpos_t begin, textpos_t end);
    bool                elide_selected_text();

    // Last command tracking.
    const char*         get_last_command_name() const noexcept { return m_edit->m_last_command_name.c_str(); }
    editor_command_func_t get_last_command_func() const noexcept { return m_edit->m_last_command_func; }
    void                set_last_command(const char* name, editor_command_func_t func);

    // Named values (allows commands to store state in the editor_context).
    const char*         get_named_value(const char* name) const;
    int32_t             get_named_value_int(const char* name, int32_t def=0) const;
    // Pinned values appear unset after the input change counter advances.
    void                set_named_value(const char* name, const char* value, bool pin=true);
    void                set_named_value_int(const char* name, int32_t value, bool pin=true);
    void                clear_named_value(const char* name);

#pragma region Editing operations
    // Basic editing.
    void                begin_of_input(bool select=false);
    void                end_of_input(bool select=false);
    bool                move_left(uint8_t word=0, bool select=false);
    bool                move_right(uint8_t word=0, bool select=false);
    bool                backspace(uint8_t word=0);
    bool                del(uint8_t word=0);
    void                del_line();
    bool                transpose(uint8_t word=0);

    // Mark management.
    bool                exchange_caret_and_mark();

    // Clipboard operations.
#if _WIN32
    bool                copy_to_clipboard();
    bool                cut_to_clipboard();
    bool                paste_from_clipboard();
#endif

    // Overwrite mode.
    bool                get_overwrite_mode() const noexcept { return m_edit->m_overwrite_mode; }
    void                set_overwrite_mode(bool overwrite) noexcept;
    void                toggle_overwrite_mode() noexcept { set_overwrite_mode(!m_edit->m_overwrite_mode); }

    // Numeric arguments.
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

    // Read parameter string.
    bool                start_read_parameter(parameter_request request);
    bool                is_read_parameter() const noexcept { return !!m_reading_parameter; }
    bool                accept_read_parameter(cstring& parameter);
    void                cancel_read_parameter();

    // Scrolling.
    bool                scroll_horizontally(int32_t columns, int32_t cursor_column, bool exclude_auto_scroll=true);
    bool                move_caret_vertically(int32_t rows, int32_t cursor_column, bool select=false);
    bool                get_pos_from_screen(uint32_t x, uint32_t y, textpos_t& pos, screen_scroll_info* scroll=nullptr);
    bool                set_caret_from_screen(uint32_t x, uint32_t y, uint32_t drag_scroll_chars=0, bool word_drag=false);
    void                suppress_auto_horizontal_scroll();

    // Undo.
    void                clear_undo() { init_undo(); }
    void                begin_undo_group();
    void                end_undo_group();
    bool                undo();
    bool                undo_all();
    bool                redo();
#ifdef DEBUG
    void                dump_undo_stack();
#endif

    // Keyboard macros.
    int32_t             start_kbd_macro(bool append); // Returns -1 on overflow.
    bool                end_kbd_macro();
    int32_t             play_last_kbd_macro(int32_t count=1); // Returns -1 on overflow.
    bool                is_recording_kbd_macro() const { return m_recording_macro; }
    void                discard_last_recorded_keyseq() { m_discard_last_recorded_keyseq = true; }
    bool                has_last_kbd_macro() const { return m_has_last_macro; }
    const cstring&      get_last_kbd_macro() const { return m_last_macro; } // Can contain NUL chars.
    void                clear_kbd_macro();
#pragma endregion Editing operations

    // Self-insert optimization.
    bool                get_allow_optimized_self_insert() const { return m_allow_optimized_self_insert; }
    void                set_allow_optimized_self_insert(bool allow) { m_allow_optimized_self_insert = allow; }

    // Error reporting.
    const char*         get_error_message(int32_t index) const;
    int32_t             count_error_messages() const { return int32_t(m_error_messages.size()); }
    void                clear_error_messages() { m_error_messages.clear(); }

    struct stricmp_less
    {
        using is_transparent = void;
        bool operator()(const char* lhs, const char* rhs) const
        {
            return _stricmp(lhs, rhs) < 0;
        }
    };

    static void         ensure_commands();
    static void         clear_all_commands();
    static void         register_command(const char* name, editor_command_func_t func);
    static const std::map<const char*, editor_command_func_t, stricmp_less>& get_registered_commands();
    static editor_command_func_t lookup_command(const char* name); // Global commands only.
    editor_command_func_t find_available_command(const char* name) const; // Includes contextual commands (m_reading_parameter).

private:
    uint32_t            get_input_max_length() const noexcept;
    void                init_undo();
    void                clear_undo_internal();
    void                unlink_endo_entry(undo_entry* p);
    void                begin_undo_group(bool merge);
    void                insert_raw_char(char c);
    void                clear_overwrite_input();
    void                apply_message_text();
    void                apply_argument_bindings();

private:
    // Configuration.
    editor_callbacks*   m_callbacks = nullptr;      // Borrowed.
    layout_info         m_layout;   // REVIEW: does tib_context actually need access to this?
    style_info          m_style;    // REVIEW: does tib_context actually need access to this?
    editor_quirks       m_quirks;
    uint32_t            m_max_length = int16_max;

    // Input state.
    input_buffer        m_buffer;
    std::unique_ptr<editing_state> m_edit;

    // State.
    uint16_t            m_terminal_row = 0;
#if 0
    MouseHelper         m_mouse_helper;
#endif
    bool                m_done = false;
    bool                m_can_drag = false;
    bool                m_allow_optimized_self_insert = true;
    int32_t             m_dispatching = 0;
    bool                m_in_on_dispatched = false;

    // Numeric argument.
    uint8_t             m_numflags = 0;
    int8_t              m_sign_numeric_argument = 0;
    int32_t             m_numeric_argument = 0;
    int32_t             m_quoted_insert_count = 0;
    binding_handle      m_argument_bindings;

    // Read parameter string.
    struct reading_parameter
    {
        parameter_request   request;
        binding_handle      bindings;
        input_buffer        orig_buffer;
        std::unique_ptr<editing_state> orig_editing_state;
        bool                had_argument = false;
        int32_t             orig_argument = 1;
    };
    std::unique_ptr<reading_parameter> m_reading_parameter;

    // Keyboard macro recording.
    cstring             m_recording;
    cstring             m_last_macro;
    bool                m_recording_macro = false;
    bool                m_has_last_macro = false;
    bool                m_discard_last_recorded_keyseq = false;

    // Display.
    display_manager     m_display;

    // History.
#if 0
    std::vector<cstring>* m_history = nullptr;
    size_t              m_history_index = 0;
    cstring             m_curr_input_history;
#endif

    // Error messages.
    std::vector<cstring> m_error_messages;

    // Commands.
    static std::vector<cstring> s_command_names;
    static std::map<const char*, editor_command_func_t, stricmp_less> s_commands;

    // Friends.
    friend int32_t self_insert(editor_context& ctx, int32_t key, const char* name, const resolved_binding* resolved) noexcept; // For m_recording.
};

struct editor_callbacks
{
    virtual void        provide_faces(const input_buffer& buffer, cstring& faces) {}
    virtual const char* get_face_def(const input_buffer& buffer, char face) { return nullptr; }
    virtual const char* get_face_hyperlink(char face) { return nullptr; }
    virtual void        do_display(editor_context& ctx) { ctx.display(); }
    virtual bool        on_dispatch(editor_context& ctx, const char* name) { return false; }
    virtual void        on_dispatched(editor_context& ctx, const char* name) {}
    virtual void        on_binding_fallback(editor_context& ctx, const binding_handle& handle_crossed) {}
};

} // namespace tib
