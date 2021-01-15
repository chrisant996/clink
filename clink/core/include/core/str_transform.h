// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "base.h"

#include <Windows.h>
#include <assert.h>

class wstr_base;

//------------------------------------------------------------------------------
enum transform_mode : int
{
    lower,
    upper,
    title,
};

//------------------------------------------------------------------------------
void str_transform(const wchar_t* in, unsigned int len, wstr_base& out, transform_mode mode);
