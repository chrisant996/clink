// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_buffer.h"
#include "line_state.h"

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
    using_history();
    remove(0, ~0u);
}

//------------------------------------------------------------------------------
void rl_buffer::begin_line()
{
    m_need_draw = true;
}

//------------------------------------------------------------------------------
void rl_buffer::end_line()
{
}

//------------------------------------------------------------------------------
const char* rl_buffer::get_buffer() const
{
    return rl_line_buffer;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::get_length() const
{
    return rl_end;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::get_cursor() const
{
    return rl_point;
}

//------------------------------------------------------------------------------
unsigned int rl_buffer::set_cursor(unsigned int pos)
{
    return rl_point = min<unsigned int>(pos, rl_end);
}

//------------------------------------------------------------------------------
bool rl_buffer::insert(const char* text)
{
    return (m_need_draw = (text[rl_insert_text(text)] == '\0'));
}

//------------------------------------------------------------------------------
bool rl_buffer::remove(unsigned int from, unsigned int to)
{
    to = min(to, get_length());
    m_need_draw = !!rl_delete_text(from, to);
    set_cursor(get_cursor());
    return m_need_draw;
}

//------------------------------------------------------------------------------
void rl_buffer::draw()
{
    if (m_need_draw)
    {
        rl_redisplay();
        m_need_draw = false;
    }
}

//------------------------------------------------------------------------------
void rl_buffer::redraw()
{
    printf("\r");
    rl_forced_update_display();
}

//------------------------------------------------------------------------------
void rl_buffer::set_need_draw()
{
    m_need_draw = true;
}

//------------------------------------------------------------------------------
void rl_buffer::begin_undo_group()
{
    rl_begin_undo_group();
}

//------------------------------------------------------------------------------
void rl_buffer::end_undo_group()
{
    rl_end_undo_group();
}

//------------------------------------------------------------------------------
bool rl_buffer::undo()
{
    return !!rl_do_undo();
}
