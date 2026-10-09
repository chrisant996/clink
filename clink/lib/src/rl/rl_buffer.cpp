// Copyright (c) 2016 Martin Ridgers
// Portions Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_buffer.h"
#include "line_state.h"
#ifdef TIB_TODO
#include "rl_commands.h"
#else
#include "rl_integration.h"
#include "display_readline.h"
#endif

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_tokeniser.h>

#include <tib.h>

//------------------------------------------------------------------------------
void rl_buffer::reset()
{
    assert(m_attached);
    clear_override();
#ifdef TIB_TODO
    rl_maybe_replace_line();

    if (_rl_saved_line_for_history)
    {
        using_history();
        rl_maybe_unsave_line();
    }
    else
    {
        // If there's not a saved line for history and there's an undo list
        // and it's the same as the current history entry, then fork the undo
        // list.  The current line undo list gets a full copy, and the current
        // history entry retains its undo list as-is.  This allows undo to
        // continue to work in the current line, without affecting the history
        // entry's undo list, and without cross-linking the undo lists (which
        // had been creating heap corruption due to double frees).
        if (current_history() && rl_undo_list)
        {
            assert(current_history()->data == rl_undo_list);
            rl_undo_list = _rl_copy_undo_list(rl_undo_list);
        }

        using_history();
    }

    remove(0, ~0u);
    assert(!rl_point);
    assert(!rl_end);
#else
    g_tib->initialize();
#endif
}

//------------------------------------------------------------------------------
void rl_buffer::begin_line()
{
    m_attached = true;
    g_tib->invalidate();
    clear_override();
}

//------------------------------------------------------------------------------
void rl_buffer::end_line()
{
    m_attached = false;
    clear_override();
}

//------------------------------------------------------------------------------
const char* rl_buffer::get_buffer() const
{
    assert(m_attached);
    if (m_override_line)
        return m_override_line;
    return g_tib->get_text().c_str();
}

//------------------------------------------------------------------------------
uint32 rl_buffer::get_length() const
{
    assert(m_attached);
    if (m_override_line)
        return m_override_len;
    return uint32(g_tib->get_length());
}

//------------------------------------------------------------------------------
uint32 rl_buffer::get_cursor() const
{
    assert(m_attached);
    if (m_override_line)
        return m_override_pos;
    return uint32(g_tib->get_caret());
}

//------------------------------------------------------------------------------
int32 rl_buffer::get_anchor() const
{
    assert(m_attached);
    const tib::textpos_t anchor = (g_tib->has_selection() ? g_tib->get_anchor() : -1);
    assert(!m_override_pos || anchor < 0);
    return anchor;
}

//------------------------------------------------------------------------------
uint32 rl_buffer::set_cursor(uint32 pos)
{
    assert(m_attached);
    pos = min(pos, get_length());
    if (m_override_line)
    {
#ifdef TIB_TODO
        assert(cua_get_anchor() < 0);
#endif
        return m_override_pos = pos;
    }
    g_tib->set_caret(tib::textpos_t(pos));
    return get_cursor();
}

//------------------------------------------------------------------------------
void rl_buffer::set_selection(uint32 anchor, uint32 pos)
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
// TODO-TIB: maybe input_box::set_selection should clamp.
    anchor = min(anchor, get_length());
    pos = min(pos, get_length());
    g_tib->set_selection(tib::textpos_t(anchor), tib::textpos_t(pos));
}

//------------------------------------------------------------------------------
void rl_buffer::insert(const char* text)
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    g_tib->insert_text(text);
}

//------------------------------------------------------------------------------
void rl_buffer::remove(uint32 from, uint32 to)
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    from = min(from, get_length());
    to = min(to, get_length());
    const tib::textpos_t begin = min(from, to);
    const tib::textpos_t end = max(from, to);
    g_tib->remove_text(begin, end);
}

//------------------------------------------------------------------------------
void rl_buffer::draw()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    display_readline();
}

//------------------------------------------------------------------------------
void rl_buffer::redraw()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    want_redisplay_readline();
    display_readline();
}

//------------------------------------------------------------------------------
void rl_buffer::set_need_draw()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    want_redisplay_readline();
}

//------------------------------------------------------------------------------
void rl_buffer::begin_undo_group()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    g_tib->begin_undo_group();
}

//------------------------------------------------------------------------------
void rl_buffer::end_undo_group()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    g_tib->end_undo_group();
}

//------------------------------------------------------------------------------
bool rl_buffer::undo()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return false;
    return g_tib->undo();
}

//------------------------------------------------------------------------------
bool rl_buffer::has_override() const
{
    return !!m_override_line;
}

//------------------------------------------------------------------------------
void rl_buffer::clear_override()
{
    m_override_line = nullptr;
    m_override_len = 0;
    m_override_pos = 0;
}

//------------------------------------------------------------------------------
void rl_buffer::override(const char* line, int32 pos)
{
    if (!line)
    {
        clear_override();
        return;
    }
    assert(!m_override_line);
    m_override_line = line;
    m_override_len = uint32(strlen(line));
    m_override_pos = min<uint32>(pos, m_override_len);
}

//------------------------------------------------------------------------------
line_buffer_fingerprint rl_buffer::get_fingerprint(bool include_cursor) const
{
    line_buffer_fingerprint fp;
    fp.m_cursor = include_cursor ? get_cursor() : 0;
    fp.m_gen_id = g_tib->get_change_counter();
    return fp;
}
