// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"

namespace tib {

struct selection_state
{
                    selection_state() : m_anchor(0), m_caret(0) { reset_word_anchor(); }
                    selection_state(textpos_t caret) : m_anchor(caret), m_caret(caret) { reset_word_anchor(); }
                    selection_state(textpos_t anchor, textpos_t caret) : m_anchor(anchor), m_caret(caret) { reset_word_anchor(); }
    selection_state& operator=(const selection_state& other);

    bool            set_caret(textpos_t caret) { return set_selection(caret, caret); }
    bool            set_mark(textpos_t mark);
    bool            set_mark_active(bool active=true);
    bool            set_selection(textpos_t anchor, textpos_t caret);
    bool            clear_selection();
    void            reset_word_anchor() { m_word_anchor_begin = m_anchor; m_word_anchor_end = m_caret; }
    void            reset_word_anchor(textpos_t caret) { m_word_anchor_begin = m_anchor; m_word_anchor_end = caret; }

    textpos_t       get_anchor() const { return m_anchor; }
    textpos_t       get_caret() const { return m_caret; }
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
    textpos_t       m_anchor;
    textpos_t       m_caret;
    textpos_t       m_mark;
    textpos_t       m_word_anchor_begin;
    textpos_t       m_word_anchor_end;
    bool            m_mark_active = false;
    bool            m_dirty = false;
    uint32_t        m_navigation_counter = 0;
};

class input_buffer
{
    friend class input_buffer_mutate_scope;

    class input_buffer_mutate_scope
    {
    public:
        input_buffer_mutate_scope(input_buffer& buffer, const char* text)
            : m_buffer(buffer)
            , m_orig_len(buffer.m_text.length())
            , m_mutated(text && *text)
        {
            if (m_mutated)
                buffer.m_text.append(text);
        }
        ~input_buffer_mutate_scope()
        {
            if (m_mutated)
                m_buffer.m_text.set_length(m_orig_len);
        }
    private:
        input_buffer& m_buffer;
        const size_t m_orig_len;
        const bool m_mutated;
    };

public:
                        ~input_buffer() = default;
                        input_buffer() = default;

    textpos_t           get_caret() const { return m_selection.get_caret(); }
    const selection_state& get_selection_state() const { return m_selection; }

    const cstring&      get_text() const { return m_text; }
    uint32_t            get_change_counter() const { return m_change_counter; }

    // This is advertised as const, but really mutates m_text temporarily.
    input_buffer_mutate_scope scoped_raw_append(const char* text) const { return input_buffer_mutate_scope(*const_cast<input_buffer*>(this), text); }

protected:
    cstring             m_text;
    selection_state     m_selection;
    uint32_t            m_change_counter = 1;
};

} // namespace tib
