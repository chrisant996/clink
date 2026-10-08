// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class input_idle;
class key_tester;
class str_base;

//------------------------------------------------------------------------------
class terminal_in
{
public:
    enum : int32
    {
        input_error             = -1,
        input_eof               = 0x0100,
        input_terminal_resize   = 0x0200,
        input_none              = 0x0300,
        input_abort             = 0x0400,
        input_exit              = 0x0500,
    };

    inline static bool is_input_byte(int32 c) { return !(c & 0xffffff00); }
    inline static bool is_input_event(int32 c) { return (c & 0x00000f00) && !(c & 0xfffff0ff); }

    virtual         ~terminal_in() = default;
    virtual int32   begin(bool can_hide_cursor=true) = 0;
    virtual int32   end(bool can_show_cursor=true) = 0;
    virtual void    override_handle() {}
    virtual bool    available(uint32 timeout) = 0;
    virtual void    select(input_idle* callback=nullptr, uint32 timeout=INFINITE) = 0;
    virtual int32   read() = 0;
    virtual int32   peek() = 0;
    virtual bool    send_terminal_request(const char* request, const char* response_prefix, const char* response_final, str_base& out, uint32 timeout1=500, uint32 timeout2=500) { return false; }
    virtual key_tester* set_key_tester(key_tester* keys) = 0;
};
