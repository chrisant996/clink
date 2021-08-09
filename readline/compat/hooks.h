// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#if !defined(S_IFLNK)
# if defined(__cplusplus) || defined(_MSC_VER)
static_assert((_S_IFMT & ~0xF000) == 0, "_S_IFMT has bits outside 0xF000");
static_assert(_S_IFMT == S_IFMT, "_S_IFMT is not equal to S_IFMT");
# else
_Static_assert((_S_IFMT & ~0xF000) == 0, "_S_IFMT has bits outside 0xF000");
_Static_assert(_S_IFMT == S_IFMT, "_S_IFMT is not equal to S_IFMT");
# endif
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
