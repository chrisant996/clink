// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class input_idle;
class key_tester;

//------------------------------------------------------------------------------
class terminal_in
{
public:
    enum : int32 {
        input_none              = int32(0x80000000),
        input_abort,
        input_terminal_resize,
        input_exit,
    };

    virtual         ~terminal_in() = default;
    virtual int32   begin(bool can_hide_cursor=true) = 0;
    virtual int32   end(bool can_show_cursor=true) = 0;
    virtual bool    available(uint32 timeout) = 0;
    virtual void    select(input_idle* callback=nullptr, uint32 timeout=INFINITE) = 0;
    virtual int32   read() = 0;
    virtual int32   peek() = 0;
    virtual key_tester* set_key_tester(key_tester* keys) = 0;
};
