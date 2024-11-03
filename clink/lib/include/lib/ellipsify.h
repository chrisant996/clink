// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

typedef void (*vstrlen_func_t)(const char* s, int32 len);
enum ellipsify_mode { INVALID=-1, RIGHT, LEFT, PATH };

int32 ellipsify(const char* in, int32 limit, str_base& out, bool expand_ctrl);
int32 ellipsify_to_callback(const char* in, int32 limit, int32 expand_ctrl, vstrlen_func_t callback);
int32 ellipsify_ex(const char* in, int32 limit, ellipsify_mode mode, str_base& out, const char* ellipsis=nullptr, bool expand_ctrl=false, bool* truncated=nullptr);

extern const char* const ellipsis;
#ifdef USE_ASCII_ELLIPSIS
const int32 ellipsis_len = 3;
const int32 ellipsis_cells = 3;
#else
const int32 ellipsis_len = 3;
const int32 ellipsis_cells = 1;
#endif
