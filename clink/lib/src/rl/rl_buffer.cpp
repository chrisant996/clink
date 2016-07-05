// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_buffer.h"

#include <core/base.h>

extern "C" {
#include <readline/readline.h>
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
    return (m_need_draw = !!rl_delete_text(from, to));
}

//------------------------------------------------------------------------------
void rl_buffer::draw()
{
    if (m_need_draw)
        rl_redisplay();
}

//------------------------------------------------------------------------------
void rl_buffer::redraw()
{
    rl_forced_update_display();
}
