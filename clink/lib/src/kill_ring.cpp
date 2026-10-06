// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "kill_ring.h"

#include <core/str.h>

//------------------------------------------------------------------------------
constexpr uint32 c_capacity = 10;
static str_moveable s_kill_ring[c_capacity];
static uint32 s_head = 0;
static uint32 s_count = 0;

//------------------------------------------------------------------------------
uint32 get_kill_ring_count()
{
    return s_count;
}

//------------------------------------------------------------------------------
uint32 get_kill_ring_index()
{
    return (s_head + s_count - !!s_count) % c_capacity;
}

//------------------------------------------------------------------------------
const char* get_kill_ring_text(uint32 index)
{
    if (index >= s_count)
        return nullptr;
    return s_kill_ring[(s_head + index) % c_capacity].c_str();
}

//------------------------------------------------------------------------------
void add_to_kill_ring(const char* text, size_t len)
{
    if (len == size_t(-1))
        len = str_len(text);

    const bool rotate = (s_count >= c_capacity);
    if (rotate)
        s_head = (s_head + 1) % c_capacity;
    else
        ++s_count;

    const uint32 index = get_kill_ring_index();
    s_kill_ring[index].clear();
    s_kill_ring[index].concat(text, int32(len));
}
