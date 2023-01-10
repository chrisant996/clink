/* gettimeofday.c - gettimeofday replacement using time() */

/* Copyright (C) 2020, 2022 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Bash is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#if !defined (HAVE_GETTIMEOFDAY)

#include "posixtime.h"
#if HAVE_STDINT_H
#include <stdint.h>
#endif

/* A version of gettimeofday that just sets tv_sec from time(3) on Unix-like
   systems that don't have it, or a version for Win32 systems. */
int
gettimeofday (struct timeval *restrict tv, void *restrict tz)
{
#if !defined (_WIN32)
  tv->tv_sec = (time_t) time ((time_t *)0);
  tv->tv_usec = 0;
#else
  /* EPOCH is the number of 100 nanosecond intervals from
    January 1, 1601 (UTC) to January 1, 1970.
    (the correct value has 9 trailing zeros) */
  static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

  SYSTEMTIME  system_time;
  FILETIME    file_time;
  uint64_t    time;

  GetSystemTime(&system_time);
  SystemTimeToFileTime(&system_time, &file_time);
  time =  ((uint64_t)file_time.dwLowDateTime);
  time += ((uint64_t)file_time.dwHighDateTime) << 32;

  tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
  tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
#endif

  return 0;
}
#endif
