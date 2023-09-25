// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "scroll_car.h"

//------------------------------------------------------------------------------
int32 calc_scroll_car_size(int32 visible, int32 total)
{
    if (visible <= 0 || visible >= total)
        return 0;

    const int car_size = max(1, min(visible, (visible * visible + (total / 2)) / total));
    return car_size;
}

//------------------------------------------------------------------------------
int32 calc_scroll_car_offset(int32 position, int32 visible, int32 total, int32 car_size)
{
    if (car_size <= 0)
        return 0;

    const int32 car_positions = (visible + 1 - car_size);
    if (car_positions <= 0)
        return 0;

    const double per_car_position = double(total - visible) / double(car_positions);
    if (per_car_position <= 0)
        return 0;

    const int32 car_offset = min(visible - car_size, int32(double(position) / per_car_position));
    return car_offset;
}
