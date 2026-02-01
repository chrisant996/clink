// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_queue.h"
#include "doskey.h"
#include "intercept.h"
#include <core/os.h>

//------------------------------------------------------------------------------
static void strip_line_endings(str_base& line)
{
    while (line.length() > 0)
    {
        const uint32 len_minus_one = (line.length() - 1);
        const char c = line.c_str()[len_minus_one];
        if (c != '\r' && c != '\n')
            break;
        line.truncate(len_minus_one);
    }
}

//------------------------------------------------------------------------------
static void ensure_crlf(str_base& line)
{
    strip_line_endings(line);
    line.concat("\r\n");
}

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
void line_queue::enqueue_back(const char* line)
{
    if (!line || !*line)
        return;

    if (!m_queued_lines.empty())
    {
        auto& back = m_queued_lines.back();
        if (back.m_line.length() && back.m_line.c_str()[back.m_line.length() - 1] != '\n')
        {
            assert(!check_dequeue_flag(back.m_flags, dequeue_flags::hide_prompt));
            assert(check_dequeue_flag(back.m_flags, dequeue_flags::show_line));
            back.m_line.concat(line);
            if (back.m_wchar_cursor > 0)
            {
                wstr_moveable wline(line);
                back.m_wline.concat(wline.c_str());
            }
            return;
        }
    }

    str_moveable s(line);
    queued_line tmp(std::move(s), dequeue_flags::show_line);
    m_queued_lines.emplace_back(std::move(tmp));
}

//------------------------------------------------------------------------------
void line_queue::enqueue_front(std::list<queued_line>& lines)
{
    for (auto iter = lines.rbegin(); iter != lines.rend(); ++iter)
    {
        assert(!check_dequeue_flag(iter->m_flags, dequeue_flags::edit_line));
        iter->m_flags &= ~dequeue_flags::edit_line;

        // It's nonsensical to hide the prompt and show the line.
        assert((iter->m_flags & (dequeue_flags::hide_prompt|dequeue_flags::show_line)) != (dequeue_flags::hide_prompt|dequeue_flags::show_line));

        // Lines at the front must always be terminated with a new line.
        ensure_crlf(iter->m_line);

        // Insert the line.
        m_queued_lines.emplace_front(std::move(*iter));
        assert(iter->m_line.empty()); // Make sure std::move() really moved it.
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
        to_utf8(out, front.m_wline.c_str() + front.m_wchar_cursor, front.m_wline.length() - front.m_wchar_cursor);
    else
        out = front.m_line.c_str();
    flags = front.m_flags;
    if (front.m_line.length() && front.m_line.c_str()[front.m_line.length() - 1] != '\n')
    {
        flags |= dequeue_flags::edit_line;
        assert(check_dequeue_flag(flags, dequeue_flags::show_line));
    }

    std::list<str_moveable> queue;
    if (!front.m_wchar_cursor &&    // REVIEW:  Is this right?
        (flags & (dequeue_flags::hide_prompt|dequeue_flags::show_line|dequeue_flags::no_doskey)) == dequeue_flags::none)
    {
        str<> line(front.m_line.c_str());
        strip_line_endings(line);
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

    ensure_crlf(out);

    return true;
}

//------------------------------------------------------------------------------
bool line_queue::dequeue_char(wchar_t* out, bool& new_line)
{
    if (m_queued_lines.empty())
        return false;

    auto& front = m_queued_lines.front();
    if (!front.m_wchar_cursor)
        to_utf16(front.m_wline, front.m_line.c_str(), front.m_line.length());
    new_line = (!front.m_wchar_cursor && front.m_line.length() && front.m_line.c_str()[front.m_line.length() - 1] == '\n');

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
