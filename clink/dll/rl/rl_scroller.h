// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "buffer_scroller.h"

extern "C" {
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
class rl_scroller
{
public:
                        rl_scroller();
    void                begin(int count, int invoking_key);
    void                end(int count, int invoking_key);
    void                page_up(int count, int invoking_key);
    void                page_down(int count, int invoking_key);

private:
    buffer_scroller     m_scroller;
    KEYMAP_ENTRY        m_keymap[KEYMAP_SIZE];
    Keymap              m_prev_keymap;
};
