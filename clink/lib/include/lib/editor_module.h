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
        virtual int32       set_bind_group(int32 bind_group) = 0;
    };

    struct input
    {
        const char*         keys;
        uint32              len;    // Because '\0' is C-@ and is a valid input.
        uint8               id;
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
        virtual int32       get_group(const char* name=nullptr) const = 0;
        virtual int32       create_group(const char* name) = 0;
        virtual bool        bind(uint32 group, const char* chord, uint8 id, bool has_params=false) = 0;
    };

    virtual                 ~editor_module() = default;
    virtual void            bind_input(binder& binder) = 0;
    virtual void            on_begin_line(const context& context) = 0;
    virtual void            on_end_line() = 0;
    virtual void            on_input(const input& input, result& result, const context& context) = 0;
    virtual void            on_matches_changed(const context& context, const line_state& line, const char* needle) = 0;
    virtual void            on_terminal_resize(int32 columns, int32 rows, const context& context) = 0;
    virtual void            on_signal(int32 sig) = 0;
};
