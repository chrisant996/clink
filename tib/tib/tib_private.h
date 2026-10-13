// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"

namespace tib {

constexpr int32_t c_max_playback_depth  = 16;

void term_clear_playback();
int32_t term_push_playback(bool is_macro, const char* text, size_t len=c_auto_length);

}
