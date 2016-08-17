// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <lib/editor_backend.h>

//------------------------------------------------------------------------------
class host_backend
    : public editor_backend
{
public:
                    host_backend(const char* host_name);
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const char* prompt, const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_matches_changed(const context& context) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override;

private:
    const char*     m_host_name;
};
