// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>

class str_base;

void set_ctrl_wakeup_mask(uint32 mask);
void strip_wakeup_chars(wchar_t* chars, uint32 max_chars);
void strip_wakeup_chars(str_base& out);
