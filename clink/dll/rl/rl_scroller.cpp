// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_scroller.h"
#include "core/base.h"
#include "keymap_thunk.h"

//------------------------------------------------------------------------------
extern "C" {
int                         _rl_dispatch(int, Keymap);
extern int                  rl_key_sequence_length;
} // extern "C"

//------------------------------------------------------------------------------
void rl_scroller::page_up(int count, int invoking_key)
{
    if (rl_key_sequence_length < 3)
        return end(count, invoking_key);

    m_scroller.page_up();
}

//------------------------------------------------------------------------------
void rl_scroller::page_down(int count, int invoking_key)
{
    if (rl_key_sequence_length < 3)
        return end(count, invoking_key);

    m_scroller.page_down();
}

//------------------------------------------------------------------------------
void rl_scroller::begin(int count, int invoking_key)
{
    m_scroller.begin();
    m_scroller.page_up();

    m_prev_keymap = rl_get_keymap();
    rl_set_keymap(m_keymap);
}

//------------------------------------------------------------------------------
void rl_scroller::end(int count, int invoking_key)
{
    m_scroller.end();

    // Dispatch key to previous keymap, but only if it's a single key. This is
    // so users don't get disorientate when leaving scroll mode via arrow keys.
    rl_set_keymap(m_prev_keymap);
    if (invoking_key && rl_key_sequence_length == 1)
        _rl_dispatch(invoking_key, m_prev_keymap);
}

//------------------------------------------------------------------------------
rl_scroller::rl_scroller()
{
    MAKE_KEYMAP_THUNK(this, rl_scroller, end);

    // Build a keymap for handling scrolling.
    for (int i = 0; i < sizeof_array(m_keymap); ++i)
        rl_bind_key_in_map(i, end_thunk, m_keymap);

    rl_generic_bind(ISKMAP, "\x1b", (char*)m_keymap, m_keymap);
    rl_generic_bind(ISKMAP, "[", (char*)m_keymap, m_keymap);

    MAKE_KEYMAP_THUNK(this, rl_scroller, page_up);
    MAKE_KEYMAP_THUNK(this, rl_scroller, page_down);
    rl_bind_key_in_map('t', page_up_thunk, m_keymap);
    rl_bind_key_in_map('y', page_down_thunk, m_keymap);

    MAKE_KEYMAP_THUNK(this, rl_scroller, begin);
    rl_add_funmap_entry("enter-scroll-mode", begin_thunk);
}
