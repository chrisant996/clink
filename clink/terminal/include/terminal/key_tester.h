// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class key_tester
{
public:
    virtual         ~key_tester() = default;
    virtual bool    is_bound(const char* seq, int len) = 0;
};
