// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "base.h"

#include <assert.h>

class str_base;
class wstr_base;

//------------------------------------------------------------------------------
enum transform_mode : int32
{
    lower,
    upper,
    title,
};

//------------------------------------------------------------------------------
void str_transform(const char* in, uint32 len, str_base& out, transform_mode mode);
void str_transform(const wchar_t* in, uint32 len, wstr_base& out, transform_mode mode);
