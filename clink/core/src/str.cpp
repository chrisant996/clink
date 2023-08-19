// Copyright (c) Martin Ridgers, Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str.h"

constexpr char c_guard = char(uint8(0xee));
constexpr wchar_t c_wguard = 0xeeee;

static_assert(uint32(uint8(c_guard)) == uint32(0xee), "guard value truncated");

#ifndef DEBUG
#ifdef _WIN64
static_assert(sizeof(str_moveable) <= sizeof(void*) * 2, "unexpected str_moveable size");
static_assert(sizeof(wstr_moveable) <= sizeof(void*) * 2, "unexpected wstr_moveable size");
#else
static_assert(sizeof(str_moveable) <= sizeof(void*) * 3, "unexpected str_moveable size");
static_assert(sizeof(wstr_moveable) <= sizeof(void*) * 3, "unexpected wstr_moveable size");
#endif
#endif

//------------------------------------------------------------------------------
str_moveable::str_moveable()
: str_base(&m_reservation, 1)
{
#ifdef DEBUG
    m_guard = c_guard;
#endif
    clear();
    set_growable();
}

//------------------------------------------------------------------------------
#ifdef DEBUG
str_moveable::~str_moveable()
{
    assert(0 == m_reservation);
    assert(c_guard == m_guard);
}
#endif

//------------------------------------------------------------------------------
str_moveable& str_moveable::operator = (str_moveable&& s)
{
    if (s.owns_ptr())
    {
        str_base::operator=(std::move(s));
        s.reset_empty();
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
    reset_empty();
    return s;
}

//------------------------------------------------------------------------------
void str_moveable::free()
{
    attach(nullptr, 0);
    reset_empty();
}

//------------------------------------------------------------------------------
void str_moveable::reset_empty()
{
    reset_not_owned(&m_reservation, 1);
}



//------------------------------------------------------------------------------
wstr_moveable::wstr_moveable()
: wstr_base(&m_reservation, 1)
{
#ifdef DEBUG
    m_guard = c_wguard;
#endif
    clear();
    set_growable();
}

//------------------------------------------------------------------------------
#ifdef DEBUG
wstr_moveable::~wstr_moveable()
{
    assert(0 == m_reservation);
    assert(c_wguard == m_guard);
}
#endif

//------------------------------------------------------------------------------
wstr_moveable& wstr_moveable::operator = (wstr_moveable&& s)
{
    if (s.owns_ptr())
    {
        wstr_base::operator=(std::move(s));
        s.reset_empty();
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
    reset_empty();
    return s;
}

//------------------------------------------------------------------------------
void wstr_moveable::free()
{
    attach(nullptr, 0);
    reset_empty();
}

//------------------------------------------------------------------------------
void wstr_moveable::reset_empty()
{
    reset_not_owned(&m_reservation, 1);
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

//------------------------------------------------------------------------------
void make_spaces(uint32 num, str_base& out)
{
    out.clear();
    while (num)
    {
        uint32 chunk = min<uint32>(32, num);
        out.concat("                                ", chunk);
        num -= chunk;
    }
}
