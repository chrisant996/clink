// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

typedef void (*vstrlen_func_t)(const char* s, int len);

int ellipsify(const char* in, int limit, str_base& out, bool expand_ctrl);
int ellipsify_to_callback(const char* in, int limit, int expand_ctrl, vstrlen_func_t callback);

extern const char* const ellipsis;
#ifdef USE_ASCII_ELLIPSIS
const int ellipsis_len = 3;
const int ellipsis_cells = 3;
#else
const int ellipsis_len = 3;
const int ellipsis_cells = 1;
#endif
