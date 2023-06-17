// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

//------------------------------------------------------------------------------
enum class mouse_input_type : uint8
{
    none,
    left_click,
    right_click,
    double_click,
    wheel,
    hwheel,
    drag,
};

//------------------------------------------------------------------------------
class key_tester
{
public:
    virtual         ~key_tester() = default;
    virtual bool    is_bound(const char* seq, int32 len) = 0;
    virtual bool    accepts_mouse_input(mouse_input_type type) { return false; }
    virtual bool    translate(const char* seq, int32 len, str_base& out) { return false; }
    virtual void    set_keyseq_len(int32 len) {}
};
