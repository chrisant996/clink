// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "ecma48_iter.h"

#include <vector>

//------------------------------------------------------------------------------
class ecma48_wrapper
{
public:
                            ecma48_wrapper(const char* in, unsigned int wrap);
    bool                    next(str_base& out);
private:
    std::vector<str_iter>   m_lines;
    size_t                  m_next = 0;
};
