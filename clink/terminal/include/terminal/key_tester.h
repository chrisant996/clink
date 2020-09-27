// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

//------------------------------------------------------------------------------
class key_tester
{
public:
    virtual         ~key_tester() = default;
    virtual bool    is_bound(const char* seq, int len) = 0;
    virtual bool    translate(const char* seq, int len, str_base& out) { return false; }
};
