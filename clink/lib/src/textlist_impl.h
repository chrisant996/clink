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
enum class mouse_input_type : uint8;

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

    enum
    {
        max_columns = 3,
        col_padding = 2,
    };

    struct column_text
    {
        const char* column[max_columns];    // Additional columns for display.
    };

    struct addl_columns
    {
                    addl_columns(item_store& store);
        const char* get_col_text(int32 row, int32 col) const;
        int32       get_col_longest(int32 col) const;
        int32       get_col_layout_width(int32 col) const;
        const char* add_entry(const char* entry);
        void        add_columns(const char* columns);
        int32       calc_widths(int32 available);
        bool        get_any_tabs() const;
        void        clear();
    private:
        textlist_impl::item_store& m_store;
        std::vector<column_text> m_rows;
        int32       m_longest[max_columns] = {};
        int32       m_layout_width[max_columns] = {};
        bool        m_any_tabs = false;
    };

public:
                    textlist_impl(input_dispatcher& dispatcher);

    popup_results   activate(const char* title, const char** entries, int32 count, int32 index, bool reverse, textlist_mode mode, entry_info* infos, bool columns, const popup_config* config=nullptr);
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
    void            cancel(popup_result result);
    void            update_layout();
    void            update_top();
    void            update_display();
    void            set_top(int32 top);
    void            adjust_horz_offset(int32 delta);
    void            init_colors(const popup_config* config);
    void            reset();

    // Result.
    popup_results   m_results;
    bool            m_active = false;
    bool            m_reset_history_index = false;

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
    printer*        m_printer = nullptr;
    int32           m_bind_group = -1;
    del_callback_t  m_del_callback = nullptr;

    // Layout.
    int32           m_screen_cols = 0;
    int32           m_screen_rows = 0;
    int32           m_mouse_offset = 0;
    int32           m_mouse_left = 0;
    int32           m_mouse_width = 0;
    int32           m_visible_rows = 0;
    int32           m_max_num_cells = 0;
    int32           m_item_cells = 0;
    int32           m_longest_visible = 0;
    int32           m_horz_offset = 0;
    int32           m_horz_item_enabled = 0;
    int32           m_horz_column_enabled[max_columns] = {};
    int32           m_horz_scroll_range = 0;
#ifdef SHOW_VERT_SCROLLBARS
    int32           m_vert_scroll_car = 0;
    int32           m_vert_scroll_column = 0;
#endif
    str<32>         m_default_title;
    str<32>         m_override_title;
    bool            m_has_override_title = false;
    bool            m_force_clear = false;

    // Entries.
    int32           m_count = 0;
    const char**    m_entries = nullptr;    // Original entries from caller.
    entry_info*     m_infos = nullptr;      // Original entry numbers/etc from caller.
    std::vector<const char*> m_items;       // Escaped entries for display.
    int32           m_longest = 0;
    addl_columns    m_columns;

    // Current entry.
    int32           m_top = 0;
    int32           m_index = 0;
    int32           m_prev_displayed = -1;

    // Current input.
    str<16>         m_needle;
    bool            m_needle_is_number = false;
    bool            m_input_clears_needle = false;
    scroll_helper   m_scroll_helper;
#ifdef SHOW_VERT_SCROLLBARS
    bool            m_scroll_bar_clicked = false;
#endif

    // Configuration.
    uint32          m_pref_height = 0;      // Automatic.
    uint32          m_pref_width = 0;       // Automatic.
    textlist_mode   m_mode = textlist_mode::general;
    bool            m_reverse = false;
    bool            m_history_mode = false;
    bool            m_show_numbers = false;
    bool            m_win_history = false;
    bool            m_has_columns = false;
    popup_colors    m_color;

    // Content store.
    class item_store
    {
        const int32 pagesize = 65536;
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
