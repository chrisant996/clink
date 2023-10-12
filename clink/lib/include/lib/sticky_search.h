// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

bool has_sticky_search_position();

void save_sticky_search_position();
void capture_sticky_search_position();
void restore_sticky_search_position();
void clear_sticky_search_position();

bool can_sticky_search_add_history(const char* line);
