// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct hooked_stat
{
    __int64 st_size;
    int st_mode;
};

int is_hidden(const char* path);
