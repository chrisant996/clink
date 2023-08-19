// Copyright (c) Martin Ridgers, Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str.h"

//------------------------------------------------------------------------------
str_moveable& str_moveable::operator = (str_moveable&& s)
{
    if (s.owns_ptr())
    {
        str_base::operator=(std::move(s));
        s.reset_not_owned(s.m_empty, _countof(s.m_empty));
    }
    else
    {
        clear();
    }
    return *this;
}

//------------------------------------------------------------------------------
char* str_moveable::detach()
{
    char* s = data();
    reset_not_owned(m_empty, _countof(m_empty));
    return s;
}

//------------------------------------------------------------------------------
void str_moveable::free()
{
    attach(nullptr, 0);
    reset_not_owned(m_empty, _countof(m_empty));
}

//------------------------------------------------------------------------------
wstr_moveable& wstr_moveable::operator = (wstr_moveable&& s)
{
    if (s.owns_ptr())
    {
        wstr_base::operator=(std::move(s));
        s.reset_not_owned(s.m_empty, _countof(s.m_empty));
    }
    else
    {
        clear();
    }
    return *this;
}

//------------------------------------------------------------------------------
wchar_t* wstr_moveable::detach()
{
    wchar_t* s = data();
    reset_not_owned(m_empty, _countof(m_empty));
    return s;
}

//------------------------------------------------------------------------------
void wstr_moveable::free()
{
    attach(nullptr, 0);
    reset_not_owned(m_empty, _countof(m_empty));
}

//------------------------------------------------------------------------------
uint32 char_count(const char* ptr)
{
    uint32 count = 0;
    while (int32 c = *ptr++)
        // 'count' is increased if the top two MSBs of c are not 10xxxxxx
        count += (c & 0xc0) != 0x80;

    return count;
}

uint32 char_count(const wchar_t* ptr)
{
    uint32 count = 0;
    while (unsigned short c = *ptr++)
    {
        ++count;

        if ((c & 0xfc00) == 0xd800)
            if ((*ptr & 0xfc00) == 0xdc00)
                ++ptr;
    }

    return count;
}
