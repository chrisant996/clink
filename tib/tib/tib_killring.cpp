// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_context.h"
#include <assert.h>
#include <vector>

namespace tib {

constexpr uint32_t c_max_capacity = 10;
static std::vector<cstring> s_kill_ring;
static uint32_t s_index = 0;
static uint32_t s_was_kill_command = 0;

uint32_t get_kill_ring_count()
{
    return uint32_t(s_kill_ring.size());
}

uint32_t get_kill_ring_index()
{
    return s_index;
}

const char* get_kill_ring_text(uint32_t index)
{
    if (index >= s_kill_ring.size())
        return nullptr;
    return s_kill_ring[index].c_str();
}

size_t get_kill_ring_text_length(uint32_t index)
{
    if (index >= s_kill_ring.size())
        return 0;
    return s_kill_ring[index].length();
}

void add_to_kill_ring(int8_t append, const char* text, size_t len)
{
    len = resolve_auto_length(len, text);

    if (len)
    {
        if (!s_was_kill_command || s_kill_ring.empty() || append < 0/*force*/)
        {
            if (s_kill_ring.size() >= c_max_capacity)
                s_kill_ring.erase(s_kill_ring.begin());
            s_kill_ring.emplace_back("");
        }

        assert(!s_kill_ring.empty());
        s_index = uint32_t(s_kill_ring.size() - 1);

        if (s_was_kill_command)
        {
            if (append)
            {
                s_kill_ring[s_index].append(text, len);
            }
            else
            {
                cstring s;
                s.append(text, len);
                s.append(s_kill_ring[s_index].c_str(), s_kill_ring[s_index].length());
                s_kill_ring[s_index] = std::move(s);
            }
        }
        else
        {
            s_kill_ring[s_index].clear();
            s_kill_ring[s_index].append(text, len);
        }
    }

    ++s_was_kill_command;
}

void pop_kill_ring_index()
{
    if (s_kill_ring.empty())
        return;

    if (!s_index)
        s_index = uint32_t(s_kill_ring.size());
    --s_index;
}

uint32_t get_was_kill_command()
{
    return s_was_kill_command;
}

void clear_was_kill_command()
{
    s_was_kill_command = 0;
}

}
