/* rlmbutil.h -- utility functions for multibyte characters. */

/* Copyright (C) 2001-2025 Free Software Foundation, Inc.

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

#if !defined (_RL_MBUTIL_H_)
#define _RL_MBUTIL_H_

#include "rlstdc.h"

/************************************************/
/* check multibyte capability for I18N code     */
/************************************************/

/* For platforms which support the ISO C amendment 1 functionality we
   support user defined character classes.  */
   /* Solaris 2.5 has a bug: <wchar.h> must be included before <wctype.h>.  */
#if defined (HAVE_WCTYPE_H) && defined (HAVE_WCHAR_H) && defined (HAVE_LOCALE_H)
#  include <wchar.h>
#  include <wctype.h>
#  if defined (HAVE_ISWCTYPE) && \
      defined (HAVE_ISWLOWER) && \
      defined (HAVE_ISWUPPER) && \
      defined (HAVE_MBSRTOWCS) && \
      defined (HAVE_MBRTOWC) && \
      defined (HAVE_MBRLEN) && \
      defined (HAVE_TOWLOWER) && \
      defined (HAVE_TOWUPPER) && \
      defined (HAVE_WCHAR_T) && \
      defined (HAVE_WCWIDTH)
     /* system is supposed to support XPG5 */
#    define HANDLE_MULTIBYTE      1
#  endif
#endif

/* If we don't want multibyte chars even on a system that supports them, let
   the configuring user turn multibyte support off. */
#if defined (NO_MULTIBYTE_SUPPORT)
#  undef HANDLE_MULTIBYTE
#endif

/* Some systems, like BeOS, have multibyte encodings but lack mbstate_t.  */
#if HANDLE_MULTIBYTE && !defined (HAVE_MBSTATE_T)
#  define wcsrtombs(dest, src, len, ps) (wcsrtombs) (dest, src, len, 0)
#  define mbsrtowcs(dest, src, len, ps) (mbsrtowcs) (dest, src, len, 0)
#  define wcrtomb(s, wc, ps) (wcrtomb) (s, wc, 0)
#  define mbrtowc(pwc, s, n, ps) (mbrtowc) (pwc, s, n, 0)
#  define mbrlen(s, n, ps) (mbrlen) (s, n, 0)
#  define mbstate_t int
#endif

/* Make sure MB_LEN_MAX is at least 16 on systems that claim to be able to
   handle multibyte chars (some systems define MB_LEN_MAX as 1) */
#ifdef HANDLE_MULTIBYTE
#  include <limits.h>
#  if defined(MB_LEN_MAX) && (MB_LEN_MAX < 16)
#    undef MB_LEN_MAX
#  endif
#  if !defined (MB_LEN_MAX)
#    define MB_LEN_MAX 16
#  endif
#endif

/************************************************/
/* end of multibyte capability checks for I18N  */
/************************************************/

/*
 * wchar_t doesn't work for 32-bit values on Windows using MSVC
 */
#ifdef WCHAR_T_BROKEN
#  define WCHAR_T char32_t
#  define MBRTOWC mbrtoc32
#  define WCRTOMB c32rtomb
/* begin_clink_change */
#  undef mbrtowc
#  undef wcrtobc
#  define mbrtowc __use_MBRTOWC_instead__
#  define wcrtomb __use_WCRTOMB_instead__
/* end_clink_change */
#else	/* normal systems */
#  define WCHAR_T wchar_t
#  define MBRTOWC mbrtowc
#  define WCRTOMB wcrtomb
#endif

/*
 * Flags for _rl_find_prev_mbchar and _rl_find_next_mbchar:
 *
 * MB_FIND_ANY		find any multibyte character
 * MB_FIND_NONZERO	find a non-zero-width multibyte character
 */

#define MB_FIND_ANY	0x00
#define MB_FIND_NONZERO	0x01

extern int _rl_find_prev_mbchar (const char *, int, int);
extern int _rl_find_next_mbchar (const char *, int, int, int);

#ifdef HANDLE_MULTIBYTE

extern size_t _rl_mbstrlen (const char *);

extern int _rl_compare_chars (const char *, int, mbstate_t *, const char *, int, mbstate_t *);
extern int _rl_get_char_len (const char *, mbstate_t *);
extern int _rl_adjust_point (const char *, int, mbstate_t *);

extern int _rl_read_mbchar (char *, int);
extern int _rl_read_mbstring (int, char *, int);

extern int _rl_is_mbchar_matched (const char *, int, int, char *, int);

extern WCHAR_T _rl_char_value (const char *, int);
extern int _rl_walphabetic (WCHAR_T);

extern int _rl_mb_strcaseeqn (const char *, size_t, const char *, size_t, size_t, int);
extern int _rl_mb_charcasecmp (const char *, mbstate_t *, const char *, mbstate_t *, int);

#define _rl_to_wupper(wc)	(iswlower (wc) ? towupper (wc) : (wc))
#define _rl_to_wlower(wc)	(iswupper (wc) ? towlower (wc) : (wc))

#define MB_NEXTCHAR(b,s,c,f) \
	((MB_CUR_MAX > 1 && rl_byte_oriented == 0) \
		? _rl_find_next_mbchar ((b), (s), (c), (f)) \
		: ((s) + (c)))
#define MB_PREVCHAR(b,s,f) \
	((MB_CUR_MAX > 1 && rl_byte_oriented == 0) \
		? _rl_find_prev_mbchar ((b), (s), (f)) \
		: ((s) - 1))

#define MB_INVALIDCH(x)		((x) == (size_t)-1 || (x) == (size_t)-2)
#define MB_NULLWCH(x)		((x) == 0)

/* Try and shortcut the printable ascii characters to cut down the number of
   calls to a libc wcwidth() */
/* begin_clink_change */
#if 0
static inline int
_rl_wcwidth (WCHAR_T wc)
/* end_clink_change */
{
  switch (wc)
    {
    case L' ': case L'!': case L'"': case L'#': case L'%':
    case L'&': case L'\'': case L'(': case L')': case L'*':
    case L'+': case L',': case L'-': case L'.': case L'/':
    case L'0': case L'1': case L'2': case L'3': case L'4':
    case L'5': case L'6': case L'7': case L'8': case L'9':
    case L':': case L';': case L'<': case L'=': case L'>':
    case L'?':
    case L'A': case L'B': case L'C': case L'D': case L'E':
    case L'F': case L'G': case L'H': case L'I': case L'J':
    case L'K': case L'L': case L'M': case L'N': case L'O':
    case L'P': case L'Q': case L'R': case L'S': case L'T':
    case L'U': case L'V': case L'W': case L'X': case L'Y':
    case L'Z':
    case L'[': case L'\\': case L']': case L'^': case L'_':
    case L'a': case L'b': case L'c': case L'd': case L'e':
    case L'f': case L'g': case L'h': case L'i': case L'j':
    case L'k': case L'l': case L'm': case L'n': case L'o':
    case L'p': case L'q': case L'r': case L's': case L't':
    case L'u': case L'v': case L'w': case L'x': case L'y':
    case L'z': case L'{': case L'|': case L'}': case L'~':
      return 1;
    default:
      return wcwidth (wc);
    }
}
/* begin_clink_change */
#else
#define _rl_wcwidth wcwidth
#endif
/* end_clink_change */

/* Unicode combining characters as of version 15.1 */
#define UNICODE_COMBINING_CHAR(x) \
	(((x) >= 0x0300 && (x) <= 0x036F) || \
	 ((x) >= 0x1AB0 && (x) <= 0x1AFF) || \
	 ((x) >= 0x1DC0 && (x) <= 0x1DFF) || \
	 ((x) >= 0x20D0 && (x) <= 0x20FF) || \
	 ((x) >= 0xFE20 && (x) <= 0xFE2F))

#if defined (WCWIDTH_BROKEN)
#  define WCWIDTH(wc)	((_rl_utf8locale && UNICODE_COMBINING_CHAR((int)wc)) ? 0 : _rl_wcwidth(wc))
#else
#  define WCWIDTH(wc)	_rl_wcwidth(wc)
#endif

#if defined (WCWIDTH_BROKEN)
#  define IS_COMBINING_CHAR(x)	(WCWIDTH(x) == 0 && iswcntrl(x) == 0)
#else
#  define IS_COMBINING_CHAR(x)	(WCWIDTH(x) == 0)
#endif

#define IS_BASE_CHAR(x)		(iswgraph(x) && WCWIDTH(x) > 0)

#define UTF8_SINGLEBYTE(c)	(((c) & 0x80) == 0)
#define UTF8_MBFIRSTCHAR(c)	(((c) & 0xc0) == 0xc0)
#define UTF8_MBCHAR(c)		(((c) & 0xc0) == 0x80)

#else /* !HANDLE_MULTIBYTE */

#undef MB_LEN_MAX
#undef MB_CUR_MAX

#define MB_LEN_MAX	1
#define MB_CUR_MAX	1

#define _rl_find_prev_mbchar(b, i, f)		(((i) == 0) ? (i) : ((i) - 1))
#define _rl_find_next_mbchar(b, i1, i2, f)	((i1) + (i2))

#define _rl_char_value(buf,ind)	((buf)[(ind)])

#define _rl_walphabetic(c)	(rl_alphabetic (c))

#define _rl_to_wupper(c)	(_rl_to_upper (c))
#define _rl_to_wlower(c)	(_rl_to_lower (c))

#define MB_NEXTCHAR(b,s,c,f)	((s) + (c))
#define MB_PREVCHAR(b,s,f)	((s) - 1)

#define MB_INVALIDCH(x)		(0)
#define MB_NULLWCH(x)		(0)

#define UTF8_SINGLEBYTE(c)	(1)

#if !defined (HAVE_WCHAR_T) && !defined (wchar_t)
#  define wchar_t int
#endif

#endif /* !HANDLE_MULTIBYTE */

/* begin_clink_change */
#if 0
/* end_clink_change */
extern int rl_byte_oriented;
/* begin_clink_change */
#endif
/* end_clink_change */

/* Snagged from gnulib */
#ifdef HANDLE_MULTIBYTE
#ifdef __cplusplus
extern "C" {
#endif

/* is_basic(c) tests whether the single-byte character c is
   - in the ISO C "basic character set" or is one of '@', '$', and '`'
     which ISO C 23 § 5.2.1.1.(1) guarantees to be single-byte and in
     practice are safe to treat as basic in the execution character set,
     or
   - in the POSIX "portable character set", which
     <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap06.html>
     equally guarantees to be single-byte. */

#if (' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
    && ('$' == 36) && ('%' == 37) && ('&' == 38) && ('\'' == 39) \
    && ('(' == 40) && (')' == 41) && ('*' == 42) && ('+' == 43) \
    && (',' == 44) && ('-' == 45) && ('.' == 46) && ('/' == 47) \
    && ('0' == 48) && ('1' == 49) && ('2' == 50) && ('3' == 51) \
    && ('4' == 52) && ('5' == 53) && ('6' == 54) && ('7' == 55) \
    && ('8' == 56) && ('9' == 57) && (':' == 58) && (';' == 59) \
    && ('<' == 60) && ('=' == 61) && ('>' == 62) && ('?' == 63) \
    && ('@' == 64) && ('A' == 65) && ('B' == 66) && ('C' == 67) \
    && ('D' == 68) && ('E' == 69) && ('F' == 70) && ('G' == 71) \
    && ('H' == 72) && ('I' == 73) && ('J' == 74) && ('K' == 75) \
    && ('L' == 76) && ('M' == 77) && ('N' == 78) && ('O' == 79) \
    && ('P' == 80) && ('Q' == 81) && ('R' == 82) && ('S' == 83) \
    && ('T' == 84) && ('U' == 85) && ('V' == 86) && ('W' == 87) \
    && ('X' == 88) && ('Y' == 89) && ('Z' == 90) && ('[' == 91) \
    && ('\\' == 92) && (']' == 93) && ('^' == 94) && ('_' == 95) \
    && ('`' == 96) && ('a' == 97) && ('b' == 98) && ('c' == 99) \
    && ('d' == 100) && ('e' == 101) && ('f' == 102) && ('g' == 103) \
    && ('h' == 104) && ('i' == 105) && ('j' == 106) && ('k' == 107) \
    && ('l' == 108) && ('m' == 109) && ('n' == 110) && ('o' == 111) \
    && ('p' == 112) && ('q' == 113) && ('r' == 114) && ('s' == 115) \
    && ('t' == 116) && ('u' == 117) && ('v' == 118) && ('w' == 119) \
    && ('x' == 120) && ('y' == 121) && ('z' == 122) && ('{' == 123) \
    && ('|' == 124) && ('}' == 125) && ('~' == 126)
/* The character set is ISO-646, not EBCDIC. */
# define IS_BASIC_ASCII 1

/* All locale encodings (see localcharset.h) map the characters 0x00..0x7F
   to U+0000..U+007F, like ASCII, except for
     CP864      different mapping of '%'
     SHIFT_JIS  different mappings of 0x5C, 0x7E
     JOHAB      different mapping of 0x5C
   However, these characters in the range 0x20..0x7E are in the ISO C
   "basic character set" and in the POSIX "portable character set", which
   ISO C and POSIX guarantee to be single-byte.  Thus, locales with these
   encodings are not POSIX compliant.  And they are most likely not in use
   any more (as of 2023).  */
# define _rl_is_basic(c) ((unsigned char) (c) < 0x80)

#else

static inline int
_rl_is_basic (char c)
{
  switch (c)
    {
    case '\0':
    case '\007': case '\010':
    case '\t': case '\n': case '\v': case '\f': case '\r':
    case ' ': case '!': case '"': case '#': case '$': case '%':
    case '&': case '\'': case '(': case ')': case '*':
    case '+': case ',': case '-': case '.': case '/':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    case ':': case ';': case '<': case '=': case '>':
    case '?': case '@':
    case 'A': case 'B': case 'C': case 'D': case 'E':
    case 'F': case 'G': case 'H': case 'I': case 'J':
    case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X': case 'Y':
    case 'Z':
    case '[': case '\\': case ']': case '^': case '_': case '`':
    case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j':
    case 'k': case 'l': case 'm': case 'n': case 'o':
    case 'p': case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x': case 'y':
    case 'z': case '{': case '|': case '}': case '~':
      return 1;
    default:
      return 0;
    }
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* HANDLE_MULTIBYTE */

#endif /* _RL_MBUTIL_H_ */
