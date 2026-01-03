// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_queue.h"
#include "doskey.h"
#include "intercept.h"
#include <core/os.h>

//------------------------------------------------------------------------------
line_queue::line_queue()
    : m_doskey(os::get_shellname())
{
}

//------------------------------------------------------------------------------
void line_queue::clear()
{
    m_queued_lines.clear();
}

//------------------------------------------------------------------------------
void line_queue::enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line, enqueue_at at, bool no_doskey)
{
    // It's nonsensical to hide the prompt and show the line.
    // It's nonsensical to edit the line but not show the line.
    assert(!(hide_prompt && show_line));

    std::list<queued_line> tmp;

    for (auto& line : lines)
    {
        dequeue_flags flags = dequeue_flags::none;
        if (hide_prompt) flags |= dequeue_flags::hide_prompt;
        if (no_doskey) flags |= dequeue_flags::no_doskey;
        if (show_line)
        {
            flags |= dequeue_flags::show_line;
            if (line.empty() || line[line.length() - 1] != '\n')
                flags |= dequeue_flags::edit_line;
        }
        tmp.emplace_back(std::move(line), flags);
    };

    enqueue_lines(tmp, at);

    lines.clear();
}

//------------------------------------------------------------------------------
void line_queue::enqueue_lines(std::list<queued_line>& lines, enqueue_at at)
{
#ifdef DEBUG
#define assert_sensical(flags) \
    do { \
        /* It's nonsensical to hide the prompt and show the line. */ \
        assert((flags & (dequeue_flags::hide_prompt|dequeue_flags::show_line)) != (dequeue_flags::hide_prompt|dequeue_flags::show_line)); \
        /* It's nonsensical to edit the line but not show the line. */ \
        assert((flags & (dequeue_flags::edit_line|dequeue_flags::show_line)) != (dequeue_flags::edit_line)); \
    } while (false)
#else
#define assert_sensical(flags) do {} while (false)
#endif

    switch (at)
    {
    case enqueue_at::front:
        for (auto iter = lines.rbegin(); iter != lines.rend(); ++iter)
        {
            assert_sensical(iter->m_flags);
            m_queued_lines.emplace_front(std::move(*iter));
            assert(iter->m_line.empty()); // Make sure std::move() really moved it.
        }
        break;

    case enqueue_at::back:
        for (auto iter = lines.begin(); iter != lines.end(); ++iter)
        {
            assert_sensical(iter->m_flags);
            m_queued_lines.emplace_back(std::move(*iter));
            assert(iter->m_line.empty()); // Make sure std::move() really moved it.
        }
        break;

    default:
        assert(false);
        break;
    }

    lines.clear();
}

//------------------------------------------------------------------------------
bool line_queue::dequeue_line(str_base& out, dequeue_flags& flags)
{
    if (m_queued_lines.empty())
    {
        flags = dequeue_flags::show_line|dequeue_flags::edit_line;
        return false;
    }

    const auto& front = m_queued_lines.front();
    if (front.m_wchar_cursor)
        to_utf8(out, wstr_iter(front.m_wline.c_str() + front.m_wchar_cursor, front.m_wline.length() - front.m_wchar_cursor));
    else
        out = front.m_line.c_str();
    flags = front.m_flags;

    std::list<str_moveable> queue;
    if (!front.m_wchar_cursor &&    // REVIEW:  Is this right?
        (flags & (dequeue_flags::hide_prompt|dequeue_flags::show_line|dequeue_flags::no_doskey)) == dequeue_flags::none)
    {
        str<> line(front.m_line.c_str());
        while (line.length())
        {
            const char c = line[line.length() - 1];
            if (c == '\r' || c == '\n')
                line.truncate(line.length() - 1);
            else
                break;
        }
        doskey_alias alias;
        m_doskey.resolve(line.c_str(), alias);
        if (alias)
        {
            str_moveable next;
            if (alias.next(next))
                out = next.c_str();
            while (alias.next(next))
                queue.push_front(std::move(next));
        }
    }

    m_queued_lines.pop_front();

    for (auto& line : queue)
        m_queued_lines.emplace_front(std::move(line), dequeue_flags::hide_prompt|dequeue_flags::no_doskey);

    return true;
}

//------------------------------------------------------------------------------
bool line_queue::dequeue_char(wchar_t* out)
{
    if (m_queued_lines.empty())
        return false;

    auto& front = m_queued_lines.front();
    if (!front.m_wchar_cursor)
    {
        to_utf16(front.m_wline, str_iter(front.m_line.c_str(), front.m_line.length()));
    }

    if (front.m_wchar_cursor >= front.m_wline.length())
    {
        assert(false);
        return false;
    }

    *out = front.m_wline.c_str()[front.m_wchar_cursor++];

    if (front.m_wchar_cursor >= front.m_wline.length())
        m_queued_lines.pop_front();

    return true;
}

//------------------------------------------------------------------------------
bool line_queue::empty() const
{
    return m_queued_lines.empty();
}

//------------------------------------------------------------------------------
bool line_queue::incomplete() const
{
    if (m_queued_lines.size() != 1)
        return true;
    const auto& front = m_queued_lines.front();
    return (front.m_line.length() == 0 ||
            front.m_line.c_str()[front.m_line.length() - 1] != '\n');
}
