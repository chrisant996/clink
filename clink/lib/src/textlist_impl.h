// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "input_dispatcher.h"

#include <core/str.h>

#include <vector>

class printer;

//------------------------------------------------------------------------------
typedef const char* (*textlist_line_getter_t)(int index);

//------------------------------------------------------------------------------
class textlist_impl
    : public editor_module
{
public:
                    textlist_impl(input_dispatcher& dispatcher);

    bool            activate(editor_module::result& result, textlist_line_getter_t getter, int count);
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
    void            update_layout();
    void            update_top();
    void            update_display();
    void            set_top(int top);
    void            reset();

    // Initialization state.
    input_dispatcher& m_dispatcher;
    line_buffer*    m_buffer = nullptr;
    printer*        m_printer = nullptr;
    int             m_bind_group = -1;
    int             m_prev_bind_group = -1;

    // Layout.
    int             m_screen_cols = 0;
    int             m_screen_rows = 0;
    int             m_visible_rows = 0;
    str<32>         m_title;
    bool            m_has_title = false;

    // Entries.
    textlist_line_getter_t m_getter = nullptr;
    std::vector<const char*> m_items;
    int             m_count = 0;
    int             m_longest = 0;

    // Current entry.
    int             m_top = 0;
    int             m_index = 0;
    int             m_prev_displayed = -1;

    // Current input.
    str<16>         m_needle;
    bool            m_needle_is_number = false;

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
