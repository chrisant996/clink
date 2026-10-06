// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

uint32 get_kill_ring_count();
uint32 get_kill_ring_index();
const char* get_kill_ring_text(uint32 index);
void add_to_kill_ring(const char* text, size_t len=size_t(-1));
