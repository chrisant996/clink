// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <tib_base.h>

namespace tib {

uint32_t get_kill_ring_count();
uint32_t get_kill_ring_index();
const char* get_kill_ring_text(uint32_t index);
size_t get_kill_ring_text_length(uint32_t index);

// Pass 0 to prepend, >0 to append, and <0 to force a new index regardless of
// the get_was_kill_command() state.
void add_to_kill_ring(int8_t append, const char* text, size_t len=c_auto_length);

void pop_kill_ring_index();
uint32_t get_was_kill_command();
void clear_was_kill_command();

}
