// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#if !defined(S_IFLNK)
static_assert(_S_IFMT == 0xF000, "_S_IFMT is not the expected value");
static_assert(_S_IFMT == S_IFMT, "_S_IFMT is not equal to S_IFMT");
# define _S_IFLNK       0x0800
# define S_IFLNK        _S_IFLNK
# undef S_ISLNK
# define S_ISLNK(m)	    (((m)&S_IFLNK) == S_IFLNK)
#endif

struct hooked_stat
{
    __int64 st_size;
    int st_mode;
    short st_uid;
    short st_gid;
};
