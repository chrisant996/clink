/* shell.c -- readline utility functions that are normally provided by
	      bash when readline is linked as part of the shell. */

/* Copyright (C) 1997-2025 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library (Readline), a library
   for reading lines of text with interactive input and history editing.      

   Readline is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Readline is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Readline.  If not, see <http://www.gnu.org/licenses/>.
*/

#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <sys/types.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#if defined (HAVE_STRING_H)
#  include <string.h>
#else
#  include <strings.h>
#endif /* !HAVE_STRING_H */

#if defined (HAVE_LIMITS_H)
#  include <limits.h>
#endif

#if defined (HAVE_FCNTL_H)
#include <fcntl.h>
#endif
#if defined (HAVE_PWD_H)
#include <pwd.h>
#endif

#include <stdio.h>

#include "rlstdc.h"
#include "rlshell.h"
#include "rldefs.h"

#include "xmalloc.h"

/* begin_clink_change */
extern const char* host_get_env(const char* name);
/* end_clink_change */

#if defined (HAVE_GETPWUID) && !defined (HAVE_GETPW_DECLS)
extern struct passwd *getpwuid (uid_t);
#endif /* HAVE_GETPWUID && !HAVE_GETPW_DECLS */

#ifndef CHAR_BIT
#  define CHAR_BIT 8
#endif

/* Nonzero if the integer type T is signed.  */
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))

/* Bound on length of the string representing an integer value of type T.
   Subtract one for the sign bit if T is signed;
   302 / 1000 is log10 (2) rounded up;
   add one for integer division truncation;
   add one more for a minus sign if t is signed.  */
#define INT_STRLEN_BOUND(t) \
  ((sizeof (t) * CHAR_BIT - TYPE_SIGNED (t)) * 302 / 1000 \
   + 1 + TYPE_SIGNED (t))

/* All of these functions are resolved from bash if we are linking readline
   as part of bash. */

/* Does shell-like quoting using single quotes. */
char *
sh_single_quote (char *string)
{
  register int c;
  char *result, *r, *s;

  result = (char *)xmalloc (3 + (4 * strlen (string)));
  r = result;
  *r++ = '\'';

  for (s = string; s && (c = *s); s++)
    {
      *r++ = c;

      if (c == '\'')
	{
	  *r++ = '\\';	/* insert escaped single quote */
	  *r++ = '\'';
	  *r++ = '\'';	/* start new quoted string */
	}
    }

  *r++ = '\'';
  *r = '\0';

  return (result);
}

/* Set the environment variables LINES and COLUMNS to lines and cols,
   respectively. */
static char setenv_buf[INT_STRLEN_BOUND (int) + 1];
#if defined (HAVE_PUTENV) && !defined (HAVE_SETENV)
static char putenv_buf1[INT_STRLEN_BOUND (int) + 6 + 1];	/* sizeof("LINES=") == 6 */
static char putenv_buf2[INT_STRLEN_BOUND (int) + 8 + 1];	/* sizeof("COLUMNS=") == 8 */
#endif

void
sh_set_lines_and_columns (int lines, int cols)
{
#if defined (HAVE_SETENV)
  sprintf (setenv_buf, "%d", lines);
  setenv ("LINES", setenv_buf, 1);

  sprintf (setenv_buf, "%d", cols);
  setenv ("COLUMNS", setenv_buf, 1);
#else /* !HAVE_SETENV */
#  if defined (HAVE_PUTENV)
  sprintf (putenv_buf1, "LINES=%d", lines);
  putenv (putenv_buf1);

  sprintf (putenv_buf2, "COLUMNS=%d", cols);
  putenv (putenv_buf2);
#  endif /* HAVE_PUTENV */
#endif /* !HAVE_SETENV */
}

char *
sh_get_env_value (const char *varname)
{
/* begin_clink_change
 * getenv() doesn't seem to notice updates to the process environment unless
 * they're made through putenv(), but we need the live environment state so
 * changes made to the environment can take effect without restarting the
 * console (change to LS_COLORS, for example).
 */
#if 0
  return ((char *)getenv (varname));
#else
  return ((char *)host_get_env (varname));
#endif
/* end_clink_change */
}

char *
sh_get_home_dir (void)
{
  static char *home_dir = (char *)NULL;
  struct passwd *entry;

  if (home_dir)
    return (home_dir);

  home_dir = (char *)NULL;
#if defined (HAVE_GETPWUID)
#  if defined (__TANDEM)
  entry = getpwnam (getlogin ());
#  else
  entry = getpwuid (getuid ());
#  endif
  if (entry)
    home_dir = savestring (entry->pw_dir);
#endif

#if defined (HAVE_GETPWENT)
  endpwent ();		/* some systems need this */
#endif

  return (home_dir);
}

#if !defined (O_NDELAY)
#  if defined (FNDELAY)
#    define O_NDELAY FNDELAY
#  endif
#endif

int
sh_unset_nodelay_mode (int fd)
{
#if defined (HAVE_FCNTL)
  int flags, bflags;

  if ((flags = fcntl (fd, F_GETFL, 0)) < 0)
    return -1;

  bflags = 0;

#ifdef O_NONBLOCK
  bflags |= O_NONBLOCK;
#endif

#ifdef O_NDELAY
  bflags |= O_NDELAY;
#endif

  if (flags & bflags)
    {
      flags &= ~bflags;
      return (fcntl (fd, F_SETFL, flags));
    }
#endif

  return 0;
}
