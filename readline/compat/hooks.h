// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

// Define this to omit Readline's default display routines for the Readline input buffer.
#define OMIT_DEFAULT_DISPLAY_READLINE

// Define this to omit Readline's match display routines.
#define OMIT_DEFAULT_DISPLAY_MATCHES

#if defined(__MINGW32__) || defined(__MINGW64__)
#define HAVE_TIMEVAL 1
#include <_timeval.h>
#include <time.h>
#endif

#if !defined(S_IFLNK)
static_assert((_S_IFMT & ~0xF000) == 0, "_S_IFMT has bits outside 0xF000");
static_assert(_S_IFMT == S_IFMT, "_S_IFMT is not equal to S_IFMT");
# define _S_IFLNK       0x0800
# define S_IFLNK        _S_IFLNK
# undef S_ISLNK
# define S_ISLNK(m)	    (((m)&S_IFLNK) == S_IFLNK)
#endif

#undef S_IRUSR
#undef S_IWUSR
#undef S_IXUSR
#define S_IRUSR         (S_IREAD)       /* read, owner */
#define S_IWUSR         (S_IWRITE)      /* write, owner */
#define S_IXUSR         (S_IEXEC)       /* execute, owner */

#undef S_IRGRP
#undef S_IWGRP
#undef S_IXGRP
#define S_IRGRP         (0)             /* read, group */
#define S_IWGRP         (0)             /* write, group */
#define S_IXGRP         (0)             /* execute, group */

#undef S_IROTH
#undef S_IWOTH
#undef S_IXOTH
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

void reset_display_readline(void);
void end_prompt(int crlf);
void clear_suggestionlist_index(void);
void wait_for_input(unsigned long timeout);

#define HAVE_GETTIMEOFDAY 1
int gettimeofday(struct timeval *, struct timezone *);

// These are implemented in rl_commands.cpp.
extern const int c_clink_version;
#ifdef UNDO_LIST_HEAP_DIAGNOSTICS
struct undo_list;
typedef struct undo_list UNDO_LIST;
extern UNDO_LIST* clink_alloc_undo_entry(void);
extern void clink_free_undo_entry(UNDO_LIST* p);
extern void clink_check_undo_entry_leaks(void);
#endif

// These are implemented in os.cpp.
extern double os_clock(void);

// These are implemented in matches_lookaside.cpp.
extern int lookup_match_type(const char* match);
extern unsigned char lookup_match_flags(const char* match);
extern const char* lookup_match_display(const char* match);
extern const char* lookup_match_description(const char* match);

// These are implemented in ecma48_iter.cpp.
extern unsigned int clink_wcswidth(const char* str, unsigned int len);

// Use this in a function to prevent it from being COMDAT-folded with another
// identical function.  E.g. rl_undo_command() versus vi_undo().
extern void prevent_COMDAT_folding(const char*);
#define PREVENT_COMDAT_FOLDING() prevent_COMDAT_folding(__FUNCTION__)
