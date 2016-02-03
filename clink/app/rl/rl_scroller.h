// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/singleton.h>
#include <terminal/buffer_scroller.h>

extern "C" {
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
class rl_scroller
    : public singleton<rl_scroller>
{
public:
                        rl_scroller();
    int                 begin(int count, int invoking_key);
    int                 end(int count, int invoking_key);
    int                 page_up(int count, int invoking_key);
    int                 page_down(int count, int invoking_key);

private:
    buffer_scroller     m_scroller;
    KEYMAP_ENTRY        m_keymap[KEYMAP_SIZE];
    Keymap              m_prev_keymap;
};
