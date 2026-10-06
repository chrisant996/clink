/* nls.c -- skeletal internationalization code. */

/* Copyright (C) 1996-2022 Free Software Foundation, Inc.

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

#include <stdio.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#if defined (HAVE_LOCALE_H)
#  include <locale.h>
#endif

#if defined (HAVE_LANGINFO_CODESET)
#  include <langinfo.h>
#endif

#include <ctype.h>

#include "rldefs.h"
#include "readline.h"
#include "rlshell.h"
#include "rlprivate.h"
#include "xmalloc.h"

static int utf8locale (char *);

#define RL_DEFAULT_LOCALE "C"
static char *_rl_current_locale = 0;

#if !defined (HAVE_SETLOCALE)    
/* A list of legal values for the LANG or LC_CTYPE environment variables.
   If a locale name in this list is the value for the LC_ALL, LC_CTYPE,
   or LANG environment variable (using the first of those with a value),
   readline eight-bit mode is enabled. */
static char *legal_lang_values[] =
{
 "iso88591",
 "iso88592",
 "iso88593",
 "iso88594",
 "iso88595",
 "iso88596",
 "iso88597",
 "iso88598",
 "iso88599",
 "iso885910",
 "koi8r",
 "utf8",
  0
};

static char *normalize_codeset (char *);
#endif /* !HAVE_SETLOCALE */

static char *find_codeset (char *, size_t *);

static int
utf8locale (char *lspec)
{
  char *cp;

#if HAVE_LANGINFO_CODESET
  cp = nl_langinfo (CODESET);
  return (STREQ (cp, "UTF-8") || STREQ (cp, "utf8"));
#else
  size_t len;

  cp = find_codeset (lspec, &len);

  if (cp == 0 || len < 4 || len > 5)
    return 0;
  return ((len == 5) ? strncmp (cp, "UTF-8", len) == 0 : strncmp (cp, "utf8", 4) == 0);
#endif
}

#if !defined (HAVE_SETLOCALE)
static char *
normalize_codeset (char *codeset)
{
  size_t namelen, i;
  int len, all_digits;
  char *wp, *retval;

  codeset = find_codeset (codeset, &namelen);

  if (codeset == 0)
    return (codeset);

  all_digits = 1;
  for (len = 0, i = 0; i < namelen; i++)
    {
      if (ISALNUM ((unsigned char)codeset[i]))
	{
	  len++;
	  all_digits &= _rl_digit_p (codeset[i]);
	}
    }

  retval = (char *)malloc ((all_digits ? 3 : 0) + len + 1);
  if (retval == 0)
    return ((char *)0);

  wp = retval;
  /* Add `iso' to beginning of an all-digit codeset */
  if (all_digits)
    {
      *wp++ = 'i';
      *wp++ = 's';
      *wp++ = 'o';
    }

  for (i = 0; i < namelen; i++)
    if (ISALPHA ((unsigned char)codeset[i]))
      *wp++ = _rl_to_lower (codeset[i]);
    else if (_rl_digit_p (codeset[i]))
      *wp++ = codeset[i];
  *wp = '\0';

  return retval;
}
#endif /* !HAVE_SETLOCALE */

/* Isolate codeset portion of locale specification. */
static char *
find_codeset (char *name, size_t *lenp)
{
  char *cp, *language, *result;

  cp = language = name;
  result = (char *)0;

  while (*cp && *cp != '_' && *cp != '@' && *cp != '+' && *cp != ',')
    cp++;

  /* This does not make sense: language has to be specified.  As
     an exception we allow the variable to contain only the codeset
     name.  Perhaps there are funny codeset names.  */
  if (language == cp) 
    {
      *lenp = strlen (language);
      result = language;
    }
  else
    {
      /* Next is the territory. */
      if (*cp == '_')
	do
	  ++cp;
	while (*cp && *cp != '.' && *cp != '@' && *cp != '+' && *cp != ',' && *cp != '_');

      /* Now, finally, is the codeset. */
      result = cp;
      if (*cp == '.')
	do
	  ++cp;
	while (*cp && *cp != '@');

      if (cp - result > 2)
	{
	  result++;
	  *lenp = cp - result;
	}
      else
	{
	  *lenp = strlen (language);
	  result = language;
	}
    }

  return result;
}
