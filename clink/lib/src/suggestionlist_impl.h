// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"
#include "match_adapter.h"
#include "scroll_helper.h"

#include <core/str.h>
#include <lua/suggest.h>

#include <vector>

class printer;
enum class mouse_input_type : uint8;

//------------------------------------------------------------------------------
class suggestionlist_impl
    : public editor_module
{
public:
                    suggestionlist_impl(input_dispatcher& dispatcher);

    bool            activate(editor_module::result& result, bool reactivate);
    bool            point_within(int32 in) const;
    bool            is_active() const;
    bool            accepts_mouse_input(mouse_input_type type) const;

private:
    // editor_module.
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_terminal_resize(int32 columns, int32 rows, const context& context) override;
    virtual void    on_signal(int32 sig) override;

    // Internal methods.
    void            cancel(editor_module::result& result, bool can_reactivate=false);
    void            init_suggestions();
    void            update_layout();
    void            update_top();
    void            update_display();
    void            apply_suggestion(int32 index);
    int32           get_scroll_offset() const;
    void            set_top(int32 top);
    void            reset_top();

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
// TODO: access list of suggestions from suggestion_manager?
    std::vector<suggestion> m_suggestions;
    printer*        m_printer = nullptr;
    int32           m_bind_group = -1;
    int32           m_prev_bind_group = -1;

    // Layout.
    int32           m_screen_cols = 0;
    int32           m_screen_rows = 0;
    int32           m_mouse_offset = 0;
    int32           m_visible_rows = 0;
    int32           m_displayed_rows = 0;
#ifdef SHOW_VERT_SCROLLBARS
    int32           m_vert_scroll_car = 0;
    int32           m_vert_scroll_column = 0;
#endif
    bool            m_clear_display = false;
    bool            m_any_displayed = false;

    // Inserting matches.
// TODO: access m_line and m_started from suggestion_manager?
    bool            m_applied = false;

    // Current suggestion index.
    int32           m_top = 0;
    int32           m_index = 0;
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
bool point_in_ssuggestion_list(int32 in);
