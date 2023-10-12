// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "wakeup_chars.h"

#include <core/str.h>

#include <list>

//------------------------------------------------------------------------------
static uint32 s_dwCtrlWakeupMask = 0;
void set_ctrl_wakeup_mask(uint32 mask)
{
    s_dwCtrlWakeupMask = mask;
}

//------------------------------------------------------------------------------
template<class T> void strip_wakeup_chars_worker(T* chars, uint32 max_chars)
{
    if (!max_chars)
        return;

    T* read = chars;
    T* write = chars;

    while (max_chars--)
    {
        const T c = *read;
        if (!c)
            break;

        if (c < 0 || c >= 32 || !(s_dwCtrlWakeupMask & 1 << c))
        {
            if (write != read)
                *write = c;
            ++write;
        }

        ++read;
    }

    if (write != read)
        *write = '\0';
}

//------------------------------------------------------------------------------
void strip_wakeup_chars(wchar_t* chars, uint32 max_chars)
{
    strip_wakeup_chars_worker(chars, max_chars);
}

//------------------------------------------------------------------------------
void strip_wakeup_chars(str_base& out)
{
    uint32 max_chars = out.length();
    strip_wakeup_chars_worker(out.data(), max_chars);
}
