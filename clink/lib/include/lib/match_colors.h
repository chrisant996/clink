#pragma once

#include "matches.h"

enum indicator_no;

void parse_match_colors();
bool using_match_colors();
bool get_match_color(const char* filename, match_type type, str_base& out);
const char* get_indicator_color(indicator_no colored_filetype);
const char* get_completion_prefix_color();
bool is_colored(indicator_no colored_filetype);
void make_color(const char* seq, str_base& out);
