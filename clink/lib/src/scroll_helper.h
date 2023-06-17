// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class scroll_helper
{
public:
                    scroll_helper();
    void            clear();
    bool            can_scroll() const;
    uint32          scroll_speed() const;
    uint32          on_input();
    void            on_scroll(uint32 now);
private:
    uint32          m_scroll_tick;
    uint32          m_accelerate_tick;
    uint32          m_scroll_speed;
    bool            m_can_scroll;
};
