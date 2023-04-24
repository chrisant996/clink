// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>
#include "input_params.h"

class printer;
class pager;
class line_buffer;
class line_state;
class matches;
class word_classifications;

//------------------------------------------------------------------------------
class editor_module
{
public:
    struct result
    {
        virtual void        pass() = 0;                 // Re-dispatch the input.
        virtual void        loop() = 0;                 // Don't exit from dispatch().
        virtual void        redraw() = 0;               // Redraw the line.
        virtual void        done(bool eof=false) = 0;   // Done editing the line.
        virtual int         set_bind_group(int bind_group) = 0;
    };

    struct input
    {
        const char*         keys;
        unsigned int        len;    // Because '\0' is C-@ and is a valid input.
        unsigned char       id;
        bool                more;   // More unresolved input is pending.
        input_params        params;
    };

    struct context
    {
        const char*         prompt;
        const char*         rprompt;
        printer&            printer;
        pager&              pager;
        line_buffer&        buffer;
        const matches&      matches;
        const word_classifications& classifications;
    };

    struct binder
    {
        virtual int         get_group(const char* name=nullptr) const = 0;
        virtual int         create_group(const char* name) = 0;
        virtual bool        bind(unsigned int group, const char* chord, unsigned char id, bool has_params=false) = 0;
    };

    virtual                 ~editor_module() = default;
    virtual void            bind_input(binder& binder) = 0;
    virtual void            on_begin_line(const context& context) = 0;
    virtual void            on_end_line() = 0;
    virtual void            on_input(const input& input, result& result, const context& context) = 0;
    virtual void            on_matches_changed(const context& context, const line_state& line, const char* needle) = 0;
    virtual void            on_terminal_resize(int columns, int rows, const context& context) = 0;
    virtual void            on_signal(int sig) = 0;
};
