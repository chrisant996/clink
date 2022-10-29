// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"
#include "popup.h"
#include "scroll_helper.h"

#include <core/str.h>

#include <vector>

class printer;
enum class mouse_input_type : unsigned char;

//------------------------------------------------------------------------------
enum class textlist_mode
{
    general,
    directories,
    history,
    win_history,
};

inline bool is_history_mode(textlist_mode mode) { return mode == textlist_mode::history || mode == textlist_mode::win_history; }

//------------------------------------------------------------------------------
class textlist_impl
    : public editor_module
{
    class item_store;

    enum { max_columns = 3 };

    struct column_text
    {
        const char* column[max_columns];    // Additional columns for display.
    };

    struct addl_columns
    {
                    addl_columns(item_store& store);
        const char* get_col_text(int row, int col) const;
        int         get_col_width(int col) const;
        const char* add_entry(const char* entry);
        bool        get_any_tabs() const;
        void        clear();
    private:
        textlist_impl::item_store& m_store;
        std::vector<column_text> m_rows;
        int         m_longest[max_columns] = {};
        bool        m_any_tabs = false;
    };

public:
                    textlist_impl(input_dispatcher& dispatcher);

    popup_results   activate(const char* title, const char** entries, int count, int index, bool reverse, textlist_mode mode, entry_info* infos, bool columns, del_callback_t del_callback=nullptr);
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
    void            cancel(popup_result result);
    void            update_layout();
    void            update_top();
    void            update_display();
    void            set_top(int top);
    void            adjust_horz_offset(int delta);
    void            reset();

    // Result.
    popup_results   m_results;
    bool            m_active = false;
    bool            m_reset_history_index = false;

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
    printer*        m_printer = nullptr;
    int             m_bind_group = -1;
    del_callback_t  m_del_callback = nullptr;

    // Layout.
    int             m_screen_cols = 0;
    int             m_screen_rows = 0;
    int             m_mouse_offset = 0;
    int             m_mouse_left = 0;
    int             m_mouse_width = 0;
    int             m_visible_rows = 0;
    int             m_max_num_len = 0;
    int             m_horz_offset = 0;
    int             m_longest_visible = 0;
    str<32>         m_default_title;
    str<32>         m_override_title;
    bool            m_has_override_title = false;
    bool            m_force_clear = false;

    // Entries.
    int             m_count = 0;
    const char**    m_entries = nullptr;    // Original entries from caller.
    entry_info*     m_infos = nullptr;      // Original entry numbers/etc from caller.
    std::vector<const char*> m_items;       // Escaped entries for display.
    int             m_longest = 0;
    addl_columns    m_columns;
    textlist_mode   m_mode = textlist_mode::general;
    bool            m_reverse = false;
    bool            m_history_mode = false;
    bool            m_win_history = false;
    bool            m_has_columns = false;

    // Current entry.
    int             m_top = 0;
    int             m_index = 0;
    int             m_prev_displayed = -1;

    // Current input.
    str<16>         m_needle;
    bool            m_needle_is_number = false;
    bool            m_input_clears_needle = false;
    scroll_helper   m_scroll_helper;

    // Colors.
    struct {
        str<32> items;
        str<32> desc;
        str<32> border;
        str<32> header;
        str<32> footer;
        str<32> select;
        str<32> selectdesc;
        str<32> mark;
        str<32> selectmark;
    } m_color;

    // Content store.
    class item_store
    {
        const int pagesize = 65536;
        struct page
        {
            page*   next;
            char    data;
        };
    public:
                    ~item_store();
        const char* add(const char* item);
        void        clear();
    private:
        page*       m_page = nullptr;
        unsigned    m_front = 0;
        unsigned    m_back = 0;
    };
    item_store      m_store;
};
