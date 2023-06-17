// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

typedef void (*vstrlen_func_t)(const char* s, int32 len);

int32 ellipsify(const char* in, int32 limit, str_base& out, bool expand_ctrl);
int32 ellipsify_to_callback(const char* in, int32 limit, int32 expand_ctrl, vstrlen_func_t callback);

extern const char* const ellipsis;
#ifdef USE_ASCII_ELLIPSIS
const int32 ellipsis_len = 3;
const int32 ellipsis_cells = 3;
#else
const int32 ellipsis_len = 3;
const int32 ellipsis_cells = 1;
#endif
