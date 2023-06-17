// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "scroll_helper.h"

//------------------------------------------------------------------------------
scroll_helper::scroll_helper()
{
    clear();
}

//------------------------------------------------------------------------------
void scroll_helper::clear()
{
    m_scroll_tick = m_accelerate_tick = GetTickCount() - 0xffff;
    m_can_scroll = false;
}

//------------------------------------------------------------------------------
bool scroll_helper::can_scroll() const
{
    return m_can_scroll;
}

//------------------------------------------------------------------------------
uint32 scroll_helper::scroll_speed() const
{
    return m_scroll_speed;
}

//------------------------------------------------------------------------------
uint32 scroll_helper::on_input()
{
    const uint32 now = GetTickCount();
    m_can_scroll = (now - m_scroll_tick) > 15;
    if (now - m_scroll_tick > 250)
        m_accelerate_tick = now;
    const uint32 accelerate_duration = (now - m_accelerate_tick);
    m_scroll_speed = ((accelerate_duration > 2000) ? 10 :
                      (accelerate_duration > 1000) ? 3 : 1);
    return now;
}

//------------------------------------------------------------------------------
void scroll_helper::on_scroll(uint32 now)
{
    m_scroll_tick = now;
}