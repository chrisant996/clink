// Copyright (c) 2021 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"

#include <core/str.h>

class printer;
struct match_display_filter_entry;
class matches_iter;
enum class match_type : unsigned char;

//------------------------------------------------------------------------------
// Define FISH_ARROW_KEYS to make arrow keys move as fish shell completion.
// Otherwise arrow keys move as in powershell completion.
#define FISH_ARROW_KEYS

//------------------------------------------------------------------------------
class match_adapter
{
public:
                    ~match_adapter();
    const matches*  get_matches() const;
    void            set_matches(const matches* matches);
    void            set_regen_matches(const matches* matches);
    void            set_filtered_matches(match_display_filter_entry** filtered_matches);
    void            init_has_descriptions();

    matches_iter    get_iter();
    void            get_lcd(str_base& out) const;
    unsigned int    get_match_count() const;
    const char*     get_match(unsigned int index) const;
    const char*     get_match_display(unsigned int index) const;
    unsigned int    get_match_visible_display(unsigned int index) const;
    const char*     get_match_description(unsigned int index) const;
    unsigned int    get_match_visible_description(unsigned int index) const;
    match_type      get_match_type(unsigned int index) const;
    char            get_match_append_char(unsigned int index) const;
    unsigned char   get_match_flags(unsigned int index) const;
    bool            is_custom_display(unsigned int index) const;
    bool            is_append_display(unsigned int index) const;

    bool            is_display_filtered() const;
    bool            has_descriptions() const;

private:
    void            free_filtered();

private:
    const matches*  m_matches = nullptr;
    const matches*  m_real_matches = nullptr;
    match_display_filter_entry** m_filtered_matches = nullptr;
    unsigned int    m_filtered_count = 0;
    bool            m_has_descriptions = false;
    bool            m_filtered_has_descriptions = false;
};

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
    void            update_matches(bool restrict=false);
    void            update_len();
    void            update_layout();
    void            update_top();
    void            update_display();
    void            insert_needle();
    void            insert_match(int final=false);
    int             get_match_row(int index) const;
    int             get_longest_display() const;
    bool            use_display(bool append, match_type type, int index) const;
    void            set_top(int top);

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
    int             m_match_cols = 0;
    int             m_match_rows = 0;
    int             m_visible_rows = 0;
    bool            m_desc_below = false;
    bool            m_any_displayed = false;
    bool            m_comment_row_displayed = false;
    bool            m_can_prompt = true;
    bool            m_expanded = false;
    bool            m_clear_display = false;

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

    // Debugging.
#ifdef DEBUG
    bool            m_annotate = false;
#endif
};

//------------------------------------------------------------------------------
bool point_in_select_complete(int in);
