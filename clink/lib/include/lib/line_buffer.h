// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

struct word;

//------------------------------------------------------------------------------
class line_buffer_fingerprint
{
    friend class rl_buffer;

public:
    void clear()
    {
        memset(this, 0, sizeof(*this));
    }

    bool operator==(const line_buffer_fingerprint& o) const
    {
        return (m_cursor == o.m_cursor &&
                m_gen_id == o.m_gen_id);
    }

private:
    uint32                  m_cursor = 0;
    uint32                  m_gen_id = 0;
};

//------------------------------------------------------------------------------
class line_buffer
{
public:
    virtual                 ~line_buffer() = default;
    virtual void            reset() = 0;
    virtual void            begin_line() = 0;
    virtual void            end_line() = 0;
    virtual const char*     get_buffer() const = 0;
    virtual uint32          get_length() const = 0;
    virtual uint32          get_cursor() const = 0;
    virtual int32           get_anchor() const = 0;
    virtual line_buffer_fingerprint get_fingerprint(bool include_cursor=true) const = 0;
    virtual uint32          set_cursor(uint32 pos) = 0;
    virtual void            set_selection(uint32 anchor, uint32 pos) = 0;
    virtual bool            insert(const char* text) = 0;
    virtual bool            remove(uint32 from, uint32 to) = 0;
    virtual void            begin_undo_group() = 0;
    virtual void            end_undo_group() = 0;
    virtual bool            undo() = 0;
    virtual void            draw() = 0;
    virtual void            redraw() = 0;
    virtual void            set_need_draw() = 0;
};
