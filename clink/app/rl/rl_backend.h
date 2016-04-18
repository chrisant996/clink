// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/singleton.h>
#include <lib/editor_backend.h>
#include <lib/line_buffer.h>

//------------------------------------------------------------------------------
class rl_backend
    : public line_buffer
    , public editor_backend
    , public singleton<rl_backend>
{
public:
                            rl_backend();

private:
    virtual void            bind(binder& binder) override;
    virtual void            begin_line(const char* prompt, const context& context) override;
    virtual void            end_line() override;
    virtual void            on_matches_changed(const context& context) override;
    virtual result          on_input(const char* keys, int id, const context& context) override;
    virtual const char*     get_buffer() const override;
    virtual unsigned int    get_cursor() const override;
    virtual unsigned int    set_cursor(unsigned int pos) override;
    virtual bool            insert(const char* text) override;
    virtual bool            remove(unsigned int from, unsigned int to) override;
    virtual void            draw() override;
    virtual void            redraw() override;
    void                    done(const char* line);
    bool                    m_need_draw;
    bool                    m_done;
    bool                    m_eof;
};
