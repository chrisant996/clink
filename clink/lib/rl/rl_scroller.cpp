/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "rl_scroller.h"
#include "keymap_thunk.h"
#include "shared/util.h"

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

    rl_generic_bind(ISKMAP, "\033", (char*)m_keymap, m_keymap);
    rl_generic_bind(ISKMAP, "`", (char*)m_keymap, m_keymap);

    MAKE_KEYMAP_THUNK(this, rl_scroller, page_up);
    MAKE_KEYMAP_THUNK(this, rl_scroller, page_down);
    rl_bind_key_in_map('c', page_up_thunk, m_keymap);
    rl_bind_key_in_map('h', page_down_thunk, m_keymap);

    MAKE_KEYMAP_THUNK(this, rl_scroller, begin);
    rl_add_funmap_entry("enter-scroll-mode", begin_thunk);
}
