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
    virtual unsigned int    get_length() const override;
    virtual unsigned int    get_cursor() const override;
    virtual unsigned int    set_cursor(unsigned int pos) override;
    virtual bool            insert(const char* text) override;
    virtual bool            remove(unsigned int from, unsigned int to) override;
    virtual void            draw() override;
    virtual void            redraw() override;
    virtual void            set_need_draw() override;
    virtual void            begin_undo_group() override;
    virtual void            end_undo_group() override;
    virtual bool            undo() override;

private:
    bool                    m_need_draw;
};
