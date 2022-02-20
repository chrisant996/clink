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
}

//------------------------------------------------------------------------------
void rl_buffer::reset()
{
    assert(m_attached);
    using_history();
    remove(0, ~0u);
}

//------------------------------------------------------------------------------
void rl_buffer::begin_line()
{
    m_attached = true;
    m_need_draw = true;
}

//------------------------------------------------------------------------------
void rl_buffer::end_line()
{
    m_attached = false;
}

//------------------------------------------------------------------------------
const char* rl_buffer::get_buffer() const
{
    assert(m_attached);
    return rl_line_buffer;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::get_length() const
{
    assert(m_attached);
    return rl_end;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::get_cursor() const
{
    assert(m_attached);
    return rl_point;
}

//------------------------------------------------------------------------------
int rl_buffer::get_anchor() const
{
    assert(m_attached);
    return cua_get_anchor();
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::set_cursor(unsigned int pos)
{
    assert(m_attached);
    if (cua_clear_selection())
        m_need_draw = true;
    return rl_point = min<unsigned int>(pos, rl_end);
}

//------------------------------------------------------------------------------
void rl_buffer::set_selection(unsigned int anchor, unsigned int pos)
{
    assert(m_attached);
    if (cua_set_selection(anchor, pos))
        m_need_draw = true;
}

//------------------------------------------------------------------------------
bool rl_buffer::insert(const char* text)
{
    assert(m_attached);
    return (m_need_draw = (text[rl_insert_text(text)] == '\0'));
}

//------------------------------------------------------------------------------
bool rl_buffer::remove(unsigned int from, unsigned int to)
{
    assert(m_attached);
    to = min(to, get_length());
    m_need_draw = !!rl_delete_text(from, to);
    set_cursor(get_cursor());
    return m_need_draw;
}

//------------------------------------------------------------------------------
void rl_buffer::draw()
{
    assert(m_attached);
    if (m_need_draw)
    {
        (*rl_redisplay_function)();
        m_need_draw = false;
    }
}

//------------------------------------------------------------------------------
void rl_buffer::redraw()
{
    assert(m_attached);
    printf("\r");
    rl_forced_update_display();
}

//------------------------------------------------------------------------------
void rl_buffer::set_need_draw()
{
    assert(m_attached);
    m_need_draw = true;
}

//------------------------------------------------------------------------------
void rl_buffer::begin_undo_group()
{
    assert(m_attached);
    rl_begin_undo_group();
}

//------------------------------------------------------------------------------
void rl_buffer::end_undo_group()
{
    assert(m_attached);
    rl_end_undo_group();
}

//------------------------------------------------------------------------------
bool rl_buffer::undo()
{
    assert(m_attached);
    return !!rl_do_undo();
}
