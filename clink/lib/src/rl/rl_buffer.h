// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_buffer.h"

//------------------------------------------------------------------------------
class rl_buffer
    : public line_buffer
{
public:
    virtual void            reset() override;
    virtual void            begin_line() override;
    virtual void            end_line() override;
    virtual const char*     get_buffer() const override;
    virtual uint32          get_length() const override;
    virtual uint32          get_cursor() const override;
    virtual int32           get_anchor() const override;
    virtual uint32          set_cursor(uint32 pos) override;
    virtual void            set_selection(uint32 anchor, uint32 pos) override;
    virtual bool            insert(const char* text) override;
    virtual bool            remove(uint32 from, uint32 to) override;
    virtual void            draw() override;
    virtual void            redraw() override;
    virtual void            set_need_draw() override;
    virtual void            begin_undo_group() override;
    virtual void            end_undo_group() override;
    virtual bool            undo() override;

    bool                    has_override() const;
    void                    clear_override();
    void                    override(const char* line, int32 pos);

private:
    bool                    m_attached = false;
    const char*             m_override_line = nullptr;
    uint32                  m_override_len = 0;
    int32                   m_override_pos = 0;
};
