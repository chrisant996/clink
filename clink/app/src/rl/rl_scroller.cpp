// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_scroller.h"

#include <core/base.h>

//------------------------------------------------------------------------------
extern "C" {
int                         _rl_dispatch(int, Keymap);
extern int                  rl_key_sequence_length;
} // extern "C"

//------------------------------------------------------------------------------
rl_scroller::rl_scroller()
: m_prev_keymap(nullptr)
{
#if MODE4
    auto end_thunk = rl_delegate(this, rl_scroller, end);

    // Build a keymap for handling scrolling.
    for (int i = 0; i < sizeof_array(m_keymap); ++i)
        rl_bind_key_in_map(i, end_thunk, m_keymap);

    rl_generic_bind(ISKMAP, "\x1b", (char*)m_keymap, m_keymap);
    rl_generic_bind(ISKMAP, "[", (char*)m_keymap, m_keymap);

    auto page_up_thunk = rl_delegate(this, rl_scroller, page_up);
    auto page_down_thunk = rl_delegate(this, rl_scroller, page_down);
    rl_bind_key_in_map('t', page_up_thunk, m_keymap);
    rl_bind_key_in_map('y', page_down_thunk, m_keymap);

    auto begin_thunk = rl_delegate(this, rl_scroller, begin);
    rl_add_funmap_entry("enter-scroll-mode", begin_thunk);
#endif
}

//------------------------------------------------------------------------------
int rl_scroller::page_up(int count, int invoking_key)
{
    if (rl_key_sequence_length < 3)
        return end(count, invoking_key);

    m_scroller.page_up();
    return 1;
}

//------------------------------------------------------------------------------
int rl_scroller::page_down(int count, int invoking_key)
{
    if (rl_key_sequence_length < 3)
        return end(count, invoking_key);

    m_scroller.page_down();
    return 1;
}

//------------------------------------------------------------------------------
int rl_scroller::begin(int count, int invoking_key)
{
    m_scroller.begin();
    m_scroller.page_up();

    m_prev_keymap = rl_get_keymap();
    rl_set_keymap(m_keymap);

    return 1;
}

//------------------------------------------------------------------------------
int rl_scroller::end(int count, int invoking_key)
{
    m_scroller.end();

    // Dispatch key to previous keymap, but only if it's a single key. This is
    // so users don't get disorientate when leaving scroll mode via arrow keys.
    rl_set_keymap(m_prev_keymap);
    if (invoking_key && rl_key_sequence_length == 1)
        _rl_dispatch(invoking_key, m_prev_keymap);

    return 1;
}
