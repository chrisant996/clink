#pragma once

#include "matches.h"

#if defined(__MINGW32__) || defined(__MINGW64__)
#define enum_indicator_no int32
#else
#define enum_indicator_no enum indicator_no
#endif

void parse_match_colors();
bool using_match_colors();
bool get_match_color(const char* filename, match_type type, str_base& out);
const char* get_indicator_color(enum_indicator_no colored_filetype);
const char* get_completion_prefix_color();
bool is_colored(enum_indicator_no colored_filetype);
void make_color(const char* seq, str_base& out);
