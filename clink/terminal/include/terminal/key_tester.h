// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

//------------------------------------------------------------------------------
enum class mouse_input_type : unsigned char
{
    none,
    left_click,
    right_click,
    double_click,
    wheel,
    drag,
};

//------------------------------------------------------------------------------
class key_tester
{
public:
    virtual         ~key_tester() = default;
    virtual bool    is_bound(const char* seq, int len) = 0;
    virtual bool    accepts_mouse_input(mouse_input_type type) { return false; }
    virtual bool    translate(const char* seq, int len, str_base& out) { return false; }
    virtual void    set_keyseq_len(int len) {}
};
