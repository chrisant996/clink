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

#define S_IRUSR         (S_IREAD)       /* read, owner */
#define S_IWUSR         (S_IWRITE)      /* write, owner */
#define S_IXUSR         (S_IEXEC)       /* execute, owner */

#define S_IRGRP         (0)             /* read, group */
#define S_IWGRP         (0)             /* write, group */
#define S_IXGRP         (0)             /* execute, group */

#define S_IROTH         (0)             /* read, other */
#define S_IWOTH         (0)             /* write, other */
#define S_IXOTH         (0)             /* execute, other */

struct hooked_stat
{
    __int64 st_size;
    int st_mode;
    short st_uid;
    short st_gid;
    short st_nlink;                     /* Always 1 in MSVC */
};

void end_prompt(int crlf);
