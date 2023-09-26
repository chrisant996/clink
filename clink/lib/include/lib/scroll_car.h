// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#ifdef SHOW_VERT_SCROLLBARS
bool use_vert_scrollbars();
int32 calc_scroll_car_size(int32 rows, int32 total);
int32 calc_scroll_car_offset(int32 top, int32 rows, int32 total, int32 scroll_car);
const char* get_scroll_car_char(int32 visible_offset, int32 car_offset, int32 car_size, bool floating);
#endif
