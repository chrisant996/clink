/*
 * Copyright (c), 2016 David Aguilar
 * Based on the fnmatch implementation from OpenBSD.
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)fnmatch.h	8.1 (Berkeley) 6/2/93
 *	$OpenBSD: fnmatch.h,v 1.4 1997/09/22 05:25:32 millert Exp $
 */
#ifndef _WILDMATCH_H_
#define _WILDMATCH_H_

#ifdef __cplusplus
extern "C" {
#endif

#define WM_MATCH 0 /* Match. */
#define WM_NOMATCH 1 /* Match failed. */

#define WM_NOESCAPE 0x01 /* Disable backslash escaping. */
#define WM_PATHNAME 0x02 /* Slash must be matched by slash. */
#define WM_PERIOD 0x04 /* Period must be matched by period. */
#define WM_LEADING_DIR 0x08 /* Ignore /<tail> after Imatch. */
#define WM_CASEFOLD 0x10 /* Case insensitive search. */
#define WM_PREFIX_DIRS 0x20 /* Unused */
#define WM_WILDSTAR 0x40 /* Double-asterisks ** matches slash too. */
/* begin_clink_change */
#define WM_SLASHFOLD 0x80 /* Slash and backslash are equivalent in string. */
/* end_clink_change */

#define WM_IGNORECASE WM_CASEFOLD
#define WM_FILE_NAME WM_PATHNAME

/*
 * wildmatch is an extension of function fnmatch(3) as specified in
 * POSIX 1003.2-1992, section B.6.
 *
 * Compares a filename or pathname to a pattern.
 *
 * wildmatch is fnmatch-compatible by default.  Its new features are enabled
 * by passing WM_WILDSTAR in flags, which makes ** match across path
 * boundaries.  WM_WILDSTAR implies WM_PATHNAME and WM_PERIOD.
 *
 * The WM_ flags are the named the same as their FNM_ fnmatch counterparts
 * and are compatible in behavior to fnmatch(3) in the absence of WM_WILDSTAR.
 */

int wildmatch(const char *string, const char *pattern, int flags);


#ifdef __cplusplus
}
#endif
#endif
