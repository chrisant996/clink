// Copyright (c) 2021 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"
#include "match_adapter.h"
#include "matches_impl.h"
#include "column_widths.h"
#include "scroll_helper.h"

#include <core/str.h>

class printer;
enum class mouse_input_type : uint8;

//------------------------------------------------------------------------------
class selectcomplete_impl
    : public editor_module
{
public:
                    selectcomplete_impl(input_dispatcher& dispatcher);

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
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int32 columns, int32 rows, const context& context) override;
    virtual void    on_signal(int32 sig) override;

    // Internal methods.
    void            cancel(editor_module::result& result, bool can_reactivate=false);
    void            init_matches();
    void            update_matches();
    void            update_len(uint32 needle_len);
    void            update_layout();
    void            update_top();
    void            update_display();
    void            insert_needle();
    void            insert_match(int32 final=false);
    int32           get_match_row(int32 index) const;
    bool            use_display(bool append, match_type type, int32 index) const;
    void            set_top(int32 top);
    void            reset_top();

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
    const matches*  m_init_matches = nullptr;
    match_adapter   m_matches;
    matches_impl    m_data;
    printer*        m_printer = nullptr;
    int32           m_bind_group = -1;
    int32           m_prev_bind_group = -1;
    int32           m_delimiter = 0;
    int32           m_lcd = 0;
    int32           m_match_longest = 0;

    // Layout.
    int32           m_screen_cols = 0;
    int32           m_screen_rows = 0;
    int32           m_mouse_offset = 0;
    int32           m_match_cols = 0;
    int32           m_match_rows = 0;
    int32           m_visible_rows = 0;
    int32           m_displayed_rows = 0;
#ifdef SHOW_VERT_SCROLLBARS
    int32           m_vert_scroll_car = 0;
#endif
    bool            m_desc_below = false;
    bool            m_init_desc_below = false;
    bool            m_any_displayed = false;
    bool            m_comment_row_displayed = false;
    bool            m_can_prompt = true;
    bool            m_expanded = false;
    bool            m_clear_display = false;
    bool            m_calc_widths = false;
    column_widths   m_widths;

    // Inserting matches.
    int32           m_anchor = -1;
    int32           m_point = -1;
    int32           m_len = 0;
    bool            m_inserted = false;
    bool            m_quoted = false;

    // Current match index.
    int32           m_top = 0;
    int32           m_index = 0;
    int32           m_prev_displayed = -1;

    // Current input.
    str<>           m_needle;
    bool            m_was_backspace = false;
#ifdef FISH_ARROW_KEYS
    bool            m_prev_latched = false;
    uint8           m_prev_input_id = 0;
#endif
    scroll_helper   m_scroll_helper;

    // Debugging.
#ifdef DEBUG
    bool            m_annotate = false;
    width_t         m_col_extra = 0;
#endif
};

//------------------------------------------------------------------------------
bool point_in_select_complete(int32 in);
