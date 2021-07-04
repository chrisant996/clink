// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#if !defined(S_IFLNK)
# define _S_IFLNK    0x0800
# define S_IFLNK     _S_IFLNK
#endif

struct hooked_stat
{
    __int64 st_size;
    int st_mode;
    short st_uid;
    short st_gid;
};
