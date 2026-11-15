// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"

namespace tib {

struct selection_state
{
                    selection_state() : m_caret(0), m_anchor(0) { reset_word_anchor(); }
                    selection_state(textpos_t caret) : m_caret(caret), m_anchor(caret) { reset_word_anchor(); }
                    selection_state(textpos_t anchor, textpos_t caret) : m_caret(caret), m_anchor(caret) { reset_word_anchor(); }
    selection_state& operator=(const selection_state& other);

    bool            set_caret(textpos_t caret) { return set_selection(caret, caret); }
    bool            set_selection(textpos_t anchor, textpos_t caret);
    bool            clear_selection();
    bool            set_mark(textpos_t mark);
    bool            set_mark_active(bool active=true);
    void            reset_word_anchor() { m_word_anchor_begin = m_anchor; m_word_anchor_end = m_caret; }
    // void            reset_word_anchor(textpos_t caret) { m_word_anchor_begin = m_anchor; m_word_anchor_end = caret; }

    textpos_t       get_caret() const { return m_caret; }
    textpos_t       get_anchor() const { return m_anchor; }
    textpos_t       get_mark() const { return m_mark; }
    textpos_t       get_sel_begin() const { return min(m_anchor, m_caret); }
    textpos_t       get_sel_end() const { return max(m_anchor, m_caret); }
    textpos_t       get_word_anchor_begin() const { return m_word_anchor_begin; }
    textpos_t       get_word_anchor_end() const { return m_word_anchor_end; }
    bool            is_mark_active() const { return m_mark_active; }
    bool            has_selection() const { return m_anchor != m_caret; }

    bool            is_dirty() const { return m_dirty; }
    void            clear_dirty() { m_dirty = false; }

    uint32_t        get_navigation_counter() const { return m_navigation_counter; }

private:
    void            inc_navigation_counter();

private:
    textpos_t       m_caret;
    textpos_t       m_anchor;
    textpos_t       m_mark = 0;
    textpos_t       m_word_anchor_begin;
    textpos_t       m_word_anchor_end;
    bool            m_mark_active = false;
    bool            m_dirty = false;
    uint32_t        m_navigation_counter = 0;
};

struct input_buffer
{
    cstring             m_text;
    selection_state     m_selection;
    uint32_t            m_change_counter = 1;
};

} // namespace tib
