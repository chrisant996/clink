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
    unsigned int    scroll_speed() const;
    unsigned int    on_input();
    void            on_scroll(unsigned int now);
private:
    unsigned int    m_scroll_tick;
    unsigned int    m_accelerate_tick;
    unsigned int    m_scroll_speed;
    bool            m_can_scroll;
};
