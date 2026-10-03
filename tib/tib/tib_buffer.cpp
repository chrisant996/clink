// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib.h"
#include "wcwidth.h"
#include <assert.h>

namespace tib {

selection_state& selection_state::operator=(const selection_state& other)
{
    if (this != &other)
    {
        m_anchor = other.m_anchor;
        m_caret = other.m_caret;
        m_mark = other.m_mark;
        m_word_anchor_begin = other.m_word_anchor_begin;
        m_word_anchor_end = other.m_word_anchor_end;
        m_mark_active = other.m_mark_active;
        m_dirty = other.m_dirty;
        inc_navigation_counter();
    }
    return *this;
}

void selection_state::inc_navigation_counter()
{
    ++m_navigation_counter;
    if (!m_navigation_counter)
        ++m_navigation_counter;
}

bool selection_state::set_mark(textpos_t mark)
{
    if (m_mark == mark)
        return false;
    m_mark = mark;
    if (m_mark < 0)
        m_mark_active = false;
    return true;
}

bool selection_state::set_mark_active(bool active)
{
    if (m_mark_active == active)
        return false;
    assert(implies(active, m_mark >= 0));
    if (active && m_mark < 0)
        return false;
    m_mark_active = active;
    return true;
}

bool selection_state::set_selection(textpos_t anchor, textpos_t caret)
{
    assert(anchor >= 0);
    assert(caret >= 0);
    if (anchor < 0 || caret < 0)
        return false;
    if (anchor == m_anchor && caret == m_caret)
        return false;
    m_dirty = true;
    m_anchor = anchor;
    m_caret = caret;
    inc_navigation_counter();
    return true;
}

bool selection_state::clear_selection()
{
    if (!has_selection())
        return false;
    set_selection(m_caret, m_caret);
    return true;
}

void input_buffer::clear_selection()
{
    m_selection.clear_selection();
}

bool input_buffer::set_caret(textpos_t caret)
{
    caret = clamp_in_range(caret);
    return m_selection.set_caret(caret);
}

bool input_buffer::set_selection(textpos_t anchor, textpos_t caret)
{
    anchor = clamp_in_range(anchor);
    caret = clamp_in_range(caret);
    if (!m_selection.set_selection(anchor, caret))
        return false;
    m_selection.reset_word_anchor();
    return true;
}

bool input_buffer::extend_selection(textpos_t pos, uint8_t word)
{
    textpos_t begin;
    textpos_t end;
    get_range_at_click(pos, word, begin, end);

    textpos_t apply_begin;
    textpos_t apply_end;
    if (begin < m_selection.get_word_anchor_begin())
    {
        apply_begin = m_selection.get_word_anchor_end();
        apply_end = begin;
    }
    else if (end > m_selection.get_word_anchor_end())
    {
        apply_begin = m_selection.get_word_anchor_begin();
        apply_end = end;
    }
    else
    {
        apply_begin = m_selection.get_word_anchor_begin();
        apply_end = m_selection.get_word_anchor_end();
    }

    return m_selection.set_selection(apply_begin, apply_end);
}

void input_buffer::get_range_at_click(textpos_t pos, uint8_t word, textpos_t& begin, textpos_t& end)
{
    pos = clamp_in_range(pos);

    const textpos_t orig_pos = pos;

    // Look forward (for a word).
    pos_mover(m_text.c_str(), m_text.length(), pos, true/*forward*/, word);
    end = pos;
    pos_mover(m_text.c_str(), m_text.length(), pos, false/*forward*/, word);
    const textpos_t high_mid = pos;

    // Look backward (for a word).
    pos_mover(m_text.c_str(), m_text.length(), pos, false/*forward*/, word);
    begin = pos;
    pos_mover(m_text.c_str(), m_text.length(), pos, true/*forward*/, word);
    const textpos_t low_mid = pos;

    if (high_mid <= orig_pos)
    {
        begin = high_mid;
    }
    else if (low_mid > orig_pos)
    {
        end = low_mid;
    }
    else
    {
        // The position is in between (two words); select the text between.
        begin = low_mid;
        end = high_mid;
    }
}

bool input_buffer::select_word(bool bigword)
{
    textpos_t begin;
    textpos_t end;

    const uint8_t word = bigword ? 2 : 1;
    get_range_at_click(m_selection.get_caret(), word, begin, end);

    return set_selection(begin, end);
}

bool input_buffer::set_mark(textpos_t mark)
{
    if (mark < 0 || size_t(mark) > get_text().length())
        return false;
    if (!m_selection.set_mark(mark))
        return false;
    if (m_display && m_selection.is_mark_active())
        m_display->invalidate();
    return true;
}

bool input_buffer::set_mark_active(bool active)
{
    if (!m_selection.set_mark_active(active))
        return false;
    if (m_display)
        m_display->invalidate();
    return true;
}

void input_buffer::inc_change_counter()
{
    ++m_change_counter;
    if (!m_change_counter)
        ++m_change_counter;
}

textpos_t input_buffer::clamp_in_range(textpos_t pos) const
{
    if (pos < 0)
        pos = textpos_t(m_text.length());
    return textpos_t(min<size_t>(pos, m_text.length()));
}

} // namespace tib
