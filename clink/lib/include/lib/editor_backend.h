// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class binder;
class line_buffer;
class matches;
class terminal;

//------------------------------------------------------------------------------
class editor_backend
{
public:
    struct result
    {
        enum result_v {
            next,
            done,
            _count_v,
        };

        enum result_uc {
            more_input = _count_v,
            _count_uc,
        };

        enum result_us {
            accept_match = _count_uc,
        };

                        result(result_v result) : value(result) {}
                        result(result_uc result, unsigned char value) : value((value << 8)|result) {}
                        result(result_us result, unsigned short value) : value((value << 8)|result) {}
        uintptr_t       value;
    };

    struct context
    {
        terminal&       terminal;
        line_buffer&    buffer;
        const matches&  matches;
    };

    virtual void        bind(binder& binder) = 0;
    virtual void        begin_line() = 0;
    virtual void        end_line() = 0;
    virtual void        on_matches_changed(const context& context) = 0;
    virtual result      on_input(const char* keys, int id, const context& context) = 0;
};
