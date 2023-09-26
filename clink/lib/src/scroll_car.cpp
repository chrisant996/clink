// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "scroll_car.h"

#include <core/str.h>
#include <core/os.h>

#ifdef SHOW_VERT_SCROLLBARS

#define USE_HALF_CHARS

//------------------------------------------------------------------------------
#ifdef USE_HALF_CHARS
constexpr int32 c_min_car_size = 2;
constexpr int32 c_scale_positions = 2;
inline int32 round(int32 pos) { return (pos & ~1); }
#else
constexpr int32 c_min_car_size = 1;
constexpr int32 c_scale_positions = 1;
#endif

//------------------------------------------------------------------------------
bool use_vert_scrollbars()
{
    str<16> tmp_sb;
    return os::get_env("CLINK_USE_VERT_SCROLLBARS", tmp_sb) && atoi(tmp_sb.c_str());
}

//------------------------------------------------------------------------------
int32 calc_scroll_car_size(int32 rows, int32 total)
{
    if (rows <= 0 || rows >= total)
        return 0;

    const int car_size = max(c_min_car_size, min(rows, (rows * rows + (total / 2)) / total));
    return car_size;
}

//------------------------------------------------------------------------------
int32 calc_scroll_car_offset(int32 top, int32 rows, int32 total, int32 car_size)
{
    if (car_size <= 0)
        return 0;

    const int32 car_positions = ((rows * c_scale_positions) + 1 - car_size);
    if (car_positions <= 0)
        return 0;

    const double per_car_position = double(total - rows) / double(car_positions);
    if (per_car_position <= 0)
        return 0;

    const int32 car_offset = min((rows * c_scale_positions) - car_size, int32(double(top) / per_car_position));
    return car_offset;
}

//------------------------------------------------------------------------------
const char* get_scroll_car_char(int32 row, int32 car_offset, int32 car_size, bool floating)
{
    if (car_size <= 0)
        return nullptr;

    row *= c_scale_positions;

#ifdef USE_HALF_CHARS
    int32 full_car_size = car_size;
    if (car_offset != round(car_offset))
        ++full_car_size;
    if (car_offset + car_size != round(car_offset + car_size))
        ++full_car_size;
    full_car_size = round(full_car_size + 1);

    if (row >= round(car_offset) && row < round(car_offset) + full_car_size)
    {
        static const char* const c_car_chars[] = {
            "\xe2\x95\xbd",                                             // ╽
            "\xe2\x94\x83",                                             // ┃
            "\xe2\x95\xbf",                                             // ╿
            "\xe2\x95\xb7",                                             // ╷
            "\xe2\x94\x82",                                             // │
            "\xe2\x95\xb5",                                             // ╵
        };

        int32 index;
        if (row == round(car_offset) && // This is the first cell of the scroll car...
            row != car_offset)          // ...and the first half is not part of the car.
        {
            index = 0;
        }
        else if (row == round(car_offset + car_size) && // This is the last cell of the scroll car...
                 row != car_offset + car_size)          // ...and the second half is not part of the car.
        {
            index = 2;
        }
        else
        {
            index = 1;
        }

        if (floating)
            index += 3;

        return c_car_chars[index];
    }
#else
    if (row >= car_offset && row < car_offset + car_size)
        return floating ? "\xe2\x94\x82" : "\xe2\x94\x83";              // │ or ┃
#endif

    return nullptr;
}

//------------------------------------------------------------------------------
int32 hittest_scroll_car(int32 row, int32 rows, int32 total)
{
    if (rows <= 1 || total <= 1)
        return 0;

    return row * (total - 1) / (rows - 1);
}

#endif
