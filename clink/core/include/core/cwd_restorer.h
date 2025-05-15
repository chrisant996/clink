// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"

//------------------------------------------------------------------------------
namespace os
{

struct cwd_restorer
{
    cwd_restorer();
    ~cwd_restorer();
    str_moveable m_path;
};

}; // namespace os
