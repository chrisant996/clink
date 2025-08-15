// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"
#include "match_adapter.h"
#include "scroll_helper.h"
#include "suggestions.h"
#include "line_buffer.h"

#include <core/str.h>

#include <vector>

class printer;
enum class mouse_input_type : uint8;

//------------------------------------------------------------------------------
class suggestionlist_impl
    : public editor_module
{
public:
                    suggestionlist_impl(input_dispatcher& dispatcher);

    void            enable(bool enable);
    bool            toggle(editor_module::result& result);
    bool            point_within(int32 in) const;
    void            clear_index(bool force=false);
    bool            is_active() const;
    bool            is_active_even_if_hidden() const;
    bool            test_frozen();
    void            refresh_display(bool clear=false);
    bool            accepts_mouse_input(mouse_input_type type) const;

private:
    // editor_module.
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_need_input(int32& bind_group) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int32 columns, int32 rows, const context& context) override;
    virtual void    on_signal(int32 sig) override;

    // Internal methods.
    void            cancel(editor_module::result& result);
    void            init_suggestions();
    void            update_layout();
    void            update_top();
    void            update_display();
    void            make_sources_header(str_base& out, uint32 max_width);
    void            make_suggestion_list_string(int32 index, str_base& out, uint32 width);
    void            apply_suggestion(int32 index);
    int32           get_scroll_offset() const;
    void            set_top(int32 top);
    void            reset_top();

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
    suggestions     m_suggestions;
    int32           m_count;
    printer*        m_printer = nullptr;
    int32           m_bind_group = -1;
    int32           m_prev_bind_group = -1;
    bool            m_hide = false;
    bool            m_hide_while_fingerprint = false;
    line_buffer_fingerprint m_hide_fingerprint;

    // Configuration.
    str<16>         m_normal_color[2];      // 0=normal, 1=selected
    str<16>         m_highlight_color[2];   // 0=normal, 1=selected
    str<16>         m_markup_color[2];      // 0=normal, 1=selected

    // Layout.
    int32           m_screen_cols = 0;
    int32           m_screen_rows = 0;
    int32           m_mouse_offset = 0;
    int32           m_max_width = 0;
    int32           m_visible_rows = 0;
    int32           m_displayed_rows = 0;
#ifdef SHOW_VERT_SCROLLBARS
    int32           m_vert_scroll_car = 0;
    int32           m_vert_scroll_column = 0;
#endif
    bool            m_clear_display = false;
    bool            m_any_displayed = false;

    // Applying suggestions.
    line_buffer_fingerprint m_applied;

    // Current suggestion index.
    int32           m_top = 0;
    int32           m_index = -1;
    int32           m_prev_displayed = -1;

    // Current input.
    scroll_helper   m_scroll_helper;
#ifdef SHOW_VERT_SCROLLBARS
    bool            m_scroll_bar_clicked = false;
#endif

    // Debugging.
#ifdef DEBUG
#endif
};

//------------------------------------------------------------------------------
void update_suggestion_list_display(bool clear=false);
bool point_in_suggestion_list(int32 in);
