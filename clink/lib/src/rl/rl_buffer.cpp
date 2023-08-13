// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_buffer.h"
#include "line_state.h"
#include "rl_commands.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_tokeniser.h>

extern "C" {
#include <readline/history.h>
#include <readline/readline.h>
#include <readline/rlprivate.h> // for _rl_want_redisplay
}

// NOTE:  rl_buffer used to have a m_need_draw member, but it was independent
// from Readline's own _rl_want_redisplay variable.  That led to redundant
// display requests, which then got dynamically optimized away at runtime.
// It's more accurate and efficient to directly use _rl_want_redisplay.

//------------------------------------------------------------------------------
void rl_buffer::reset()
{
    assert(m_attached);
    clear_override();
    using_history();
    remove(0, ~0u);
}

//------------------------------------------------------------------------------
void rl_buffer::begin_line()
{
    m_attached = true;
    _rl_want_redisplay = true;
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
    return rl_line_buffer;
}

//------------------------------------------------------------------------------
uint32 rl_buffer::get_length() const
{
    assert(m_attached);
    if (m_override_line)
        return m_override_len;
    return rl_end;
}

//------------------------------------------------------------------------------
uint32 rl_buffer::get_cursor() const
{
    assert(m_attached);
    if (m_override_line)
        return m_override_pos;
    return rl_point;
}

//------------------------------------------------------------------------------
int32 rl_buffer::get_anchor() const
{
    assert(m_attached);
    assert(!m_override_pos || cua_get_anchor() < 0);
    return cua_get_anchor();
}

//------------------------------------------------------------------------------
uint32 rl_buffer::set_cursor(uint32 pos)
{
    assert(m_attached);
    if (m_override_line)
    {
        assert(cua_get_anchor() < 0);
        return m_override_pos = min<uint32>(pos, m_override_len);
    }
    if (cua_clear_selection())
        _rl_want_redisplay = true;
    return rl_point = min<uint32>(pos, rl_end);
}

//------------------------------------------------------------------------------
void rl_buffer::set_selection(uint32 anchor, uint32 pos)
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    if (cua_set_selection(anchor, pos))
        _rl_want_redisplay = true;
}

//------------------------------------------------------------------------------
bool rl_buffer::insert(const char* text)
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return false;
    return (_rl_want_redisplay = (text[rl_insert_text(text)] == '\0'));
}

//------------------------------------------------------------------------------
bool rl_buffer::remove(uint32 from, uint32 to)
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return false;
    to = min(to, get_length());
    _rl_want_redisplay = !!rl_delete_text(from, to);
    set_cursor(get_cursor());
    return !!_rl_want_redisplay;
}

//------------------------------------------------------------------------------
void rl_buffer::draw()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    if (_rl_want_redisplay)
    {
        (*rl_redisplay_function)();
        _rl_want_redisplay = false;
    }
}

//------------------------------------------------------------------------------
void rl_buffer::redraw()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    printf("\r");
    rl_forced_update_display();
}

//------------------------------------------------------------------------------
void rl_buffer::set_need_draw()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    _rl_want_redisplay = true;
}

//------------------------------------------------------------------------------
void rl_buffer::begin_undo_group()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    rl_begin_undo_group();
}

//------------------------------------------------------------------------------
void rl_buffer::end_undo_group()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return;
    rl_end_undo_group();
}

//------------------------------------------------------------------------------
bool rl_buffer::undo()
{
    assert(m_attached);
    assert(!m_override_line);
    if (m_override_line)
        return false;
    return !!rl_do_undo();
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
