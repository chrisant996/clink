#pragma once

#include "matches.h"

void parse_match_colors();
bool get_match_color(const char* filename, match_type type, str_base& out);
void make_color(const char* seq, int32 len, str_base& out);
