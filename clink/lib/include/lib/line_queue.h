// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "doskey.h"
#include <core/singleton.h>
#include <core/str.h>
#include <list>

//------------------------------------------------------------------------------
enum class dequeue_flags
{
    none            = 0x00,
    hide_prompt     = 0x01,
    show_line       = 0x02,
    edit_line       = 0x04,
    no_doskey       = 0x08,
};
DEFINE_ENUM_FLAG_OPERATORS(dequeue_flags);

inline bool check_dequeue_flag(const dequeue_flags check, const dequeue_flags mask)
{
    return (check & mask) != dequeue_flags::none;
}

//------------------------------------------------------------------------------
enum class enqueue_at { front, back };

//------------------------------------------------------------------------------
struct queued_line
{
    queued_line(str_moveable&& line, dequeue_flags flags)
        : m_line(std::move(line)), m_flags(flags) {}
    str_moveable m_line;
    wstr_moveable m_wline;
    uint32 m_wchar_cursor = 0;
    dequeue_flags m_flags;
};

//------------------------------------------------------------------------------
class line_queue
    : public singleton<line_queue>
{

public:
                    line_queue();
                    ~line_queue() = default;

    void            clear();
    void            enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line, enqueue_at at, bool no_doskey);
    void            enqueue_lines(std::list<queued_line>& lines, enqueue_at at);
    bool            dequeue_line(str_base& out, dequeue_flags& flags);
    bool            dequeue_char(wchar_t* out);
    bool            empty() const;
    bool            incomplete() const;

private:
    doskey          m_doskey;
    std::list<queued_line> m_queued_lines;
};
