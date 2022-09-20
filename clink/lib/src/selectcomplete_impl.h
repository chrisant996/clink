// Copyright (c) 2021 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"
#include "match_adapter.h"
#include "column_widths.h"
#include "scroll_helper.h"

#include <core/str.h>

class printer;
enum class mouse_input_type : unsigned char;

//------------------------------------------------------------------------------
class selectcomplete_impl
    : public editor_module
{
public:
                    selectcomplete_impl(input_dispatcher& dispatcher);

    bool            activate(editor_module::result& result, bool reactivate);
    bool            point_within(int in) const;
    bool            is_active() const;
    bool            accepts_mouse_input(mouse_input_type type) const;

private:
    // editor_module.
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override;
    virtual void    on_signal(int sig) override;

    // Internal methods.
    void            cancel(editor_module::result& result, bool can_reactivate=false);
    void            update_matches(bool restrict=false);
    void            update_len(unsigned int needle_len);
    void            update_layout();
    void            update_top();
    void            update_display();
    void            insert_needle();
    void            insert_match(int final=false);
    int             get_match_row(int index) const;
    int             get_longest_display() const;
    bool            use_display(bool append, match_type type, int index) const;
    void            set_top(int top);
    void            reset_top();

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
    match_adapter   m_matches;
    printer*        m_printer = nullptr;
    int             m_bind_group = -1;
    int             m_prev_bind_group = -1;
    int             m_delimiter = 0;
    int             m_lcd = 0;
    int             m_match_longest = 0;

    // Layout.
    int             m_screen_cols = 0;
    int             m_screen_rows = 0;
    int             m_mouse_offset = 0;
    int             m_match_cols = 0;
    int             m_match_rows = 0;
    int             m_visible_rows = 0;
    int             m_displayed_rows = 0;
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
    int             m_anchor = -1;
    int             m_point = -1;
    int             m_len = 0;
    bool            m_inserted = false;
    bool            m_quoted = false;

    // Current match index.
    int             m_top = 0;
    int             m_index = 0;
    int             m_prev_displayed = -1;

    // Current input.
    str<>           m_needle;
    bool            m_was_backspace = false;
#ifdef FISH_ARROW_KEYS
    bool            m_prev_latched = false;
    unsigned char   m_prev_input_id = 0;
#endif
    scroll_helper   m_scroll_helper;

    // Debugging.
#ifdef DEBUG
    bool            m_annotate = false;
    width_t         m_col_extra = 0;
#endif
};

//------------------------------------------------------------------------------
bool point_in_select_complete(int in);
