// Copyright (c) 2021 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"

#include <core/str.h>

class printer;

//------------------------------------------------------------------------------
class selectcomplete_impl
    : public editor_module
{
public:
                    selectcomplete_impl(input_dispatcher& dispatcher);

    bool            activate(editor_module::result& result, bool reactivate);
    bool            point_within(int in) const;
    bool            is_active() const;

private:
    // editor_module.
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override;

    // Internal methods.
    void            cancel(editor_module::result& result);
    void            update_matches(bool restrict=false, bool sort=false);
    void            update_len();
    void            update_layout();
    void            update_display();
    void            insert_needle();
    void            insert_match(bool final=false);

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
    const matches*  m_matches = nullptr;
    printer*        m_printer = nullptr;
    int             m_bind_group = -1;
    int             m_prev_bind_group = -1;
    int             m_delimiter = 0;
    int             m_lcd = 0;
    int             m_match_longest = 0;

    // Layout.
    int             m_screen_cols = 0;
    int             m_screen_rows = 0;
    int             m_match_cols = 0;
    int             m_match_rows = 0;

    // Inserting matches.
    int             m_anchor = -1;
    int             m_point = -1;
    int             m_len = 0;
    bool            m_inserted = false;
    bool            m_quoted = false;

    // Current match index.
    int             m_index = 0;

    // Current input.
    str<>           m_needle;
    bool            m_was_backspace = false;
};

//------------------------------------------------------------------------------
bool point_in_select_complete(int in);
