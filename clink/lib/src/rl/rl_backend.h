// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_backend.h"

#include <core/singleton.h>

//------------------------------------------------------------------------------
class rl_backend
    : public editor_backend
    , public singleton<rl_backend>
{
public:
                            rl_backend(const char* shell_name);

private:
    virtual void            bind(binder_func* binder) override;
    virtual void            on_begin_line(const char* prompt, const context& context) override;
    virtual void            on_end_line() override;
    virtual void            on_matches_changed(const context& context) override;
    virtual result          on_input(const char* keys, int id, const context& context) override;
    void                    done(const char* line);
    char*                   m_rl_buffer;
    bool                    m_done;
    bool                    m_eof;
};
