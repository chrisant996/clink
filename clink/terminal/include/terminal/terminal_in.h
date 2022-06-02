// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class input_idle;
class key_tester;

//------------------------------------------------------------------------------
class terminal_in
{
public:
    enum : int {
        input_none              = int(0x80000000),
        input_timeout,
        input_abort,
        input_terminal_resize,
        input_exit,
    };

    virtual         ~terminal_in() = default;
    virtual void    begin() = 0;
    virtual void    end() = 0;
    virtual void    select(input_idle* callback=nullptr) = 0;
    virtual int     read() = 0;
    virtual key_tester* set_key_tester(key_tester* keys) = 0;
};
