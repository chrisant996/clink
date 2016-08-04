// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_buffer;
class line_state;
class matches;
class terminal;

//------------------------------------------------------------------------------
class editor_backend
{
public:
    struct result
    {
        virtual void        pass() = 0;
        virtual void        redraw() = 0;
        virtual void        done(bool eof) = 0;
        virtual void        accept_match(unsigned int index) = 0;
        virtual int         set_bind_group(int bind_group) = 0;
    };

    struct input
    {
        const char*         keys;
        unsigned char       id;
    };

    struct context
    {
        terminal&           terminal;
        line_buffer&        buffer;
        const line_state&   line;
        const matches&      matches;
    };

    struct binder
    {
        virtual int         get_group(const char* name=nullptr) const = 0;
        virtual int         create_group(const char* name) = 0;
        virtual bool        bind(unsigned int group, const char* chord, unsigned char id) = 0;
    };

    virtual                 ~editor_backend() = default;
    virtual void            bind_input(binder& binder) = 0;
    virtual void            on_begin_line(const char* prompt, const context& context) = 0;
    virtual void            on_end_line() = 0;
    virtual void            on_matches_changed(const context& context) = 0;
    virtual void            on_input(const input& input, result& result, const context& context) = 0;
};
