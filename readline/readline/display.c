/* display.c -- readline redisplay facility. */

/* Copyright (C) 1987-2025 Free Software Foundation, Inc.

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

#include "posixstat.h"

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <stdio.h>

#ifdef __MSDOS__
/* begin_clink_change */
//#  include <pc.h>
/* end_clink_change */
#endif

/* System-specific feature definitions and include files. */
#include "rldefs.h"
#include "rlmbutil.h"

/* Termcap library stuff. */
#include "tcap.h"

/* Some standard library routines. */
#include "readline.h"
#include "history.h"

#include "rlprivate.h"
#include "xmalloc.h"

/* begin_clink_change
 * __MSDOS__ is used for both platform-specific file handling and terminal
 * display, but Clink has a sufficient implementation of the Termcap library.
 */
#ifdef __MSDOS__
#   undef __MSDOS__
#endif
/* end_clink_change */

static void putc_face (int, int, char *);
static void puts_face (const char *, const char *, int);
static void norm_face (char *, int);

static void cr (void);
static void redraw_prompt (char *);
static void _rl_move_cursor_relative (int, const char *, const char *);

/* Values for FLAGS */
#define PMT_MULTILINE	0x01

/* Heuristic used to decide whether it is faster to move from CUR to NEW
   by backing up or outputting a carriage return and moving forward.  CUR
   and NEW are either both buffer positions or absolute screen positions. */
#define CR_FASTER(new, cur) (((new) + 1) < ((cur) - (new)))

#define FACE_NORMAL	'0'
#define FACE_STANDOUT	'1'
#define FACE_INVALID	((char)1)
  
/* **************************************************************** */
/*								    */
/*			Display stuff				    */
/*								    */
/* **************************************************************** */

/* This is the stuff that is hard for me.  I never seem to write good
   display routines in C.  Let's see how I do this time. */

/* (PWP) Well... Good for a simple line updater, but totally ignores
   the problems of input lines longer than the screen width.

   update_line and the code that calls it makes a multiple line,
   automatically wrapping line update.  Careful attention needs
   to be paid to the vertical position variables. */

/* Keep two buffers; one which reflects the current contents of the
   screen, and the other to draw what we think the new contents should
   be.  Then compare the buffers, and make whatever changes to the
   screen itself that we should.  Finally, make the buffer that we
   just drew into be the one which reflects the current contents of the
   screen, and place the cursor where it belongs.

   Commands that want to can fix the display themselves, and then let
   this function know that the display has been fixed by setting the
   RL_DISPLAY_FIXED variable.  This is good for efficiency. */

/* Application-specific redisplay function. */
rl_voidfunc_t *rl_redisplay_function = NULL;

/* begin_clink_change */
/* Application-specific function to be called before displaying the
   input line. */
rl_voidfunc_t *rl_before_display_function = (rl_voidfunc_t *)NULL;
/* end_clink_change */

/* begin_clink_change */
const char *_rl_display_modmark_color = NULL;
const char *_rl_display_horizscroll_color = NULL;
const char *_rl_display_message_color = NULL;
static char* _rl_current_message = NULL;
static int _rl_current_message_append = 0;
/* end_clink_change */

/* Variables used to include the editing mode in the prompt. */
char *_rl_emacs_mode_str;
int _rl_emacs_modestr_len;

char *_rl_vi_ins_mode_str;
int _rl_vi_ins_modestr_len;

char *_rl_vi_cmd_mode_str;
int _rl_vi_cmd_modestr_len;

/* Pseudo-global variables declared here. */

/* Hints for other parts of readline to give to the display engine. */
int _rl_want_redisplay = 0;

static int _rl_quick_redisplay = 0;

/* Variables used only in this file. */

/* A buffer for `modeline' messages. */
static char *msg_buf = 0;

/* Non-zero forces the redisplay even if we thought it was unnecessary. */
static int forced_display;

/* set to a non-zero value by rl_redisplay if we are marking modified history
   lines and the current line is so marked. */
static int modmark;

/* Return a string indicating the editing mode, for use in the prompt. */

static char *
prompt_modestr (int *lenp)
{
  if (rl_editing_mode == emacs_mode)
    {
      if (lenp)
	*lenp = _rl_emacs_mode_str ? _rl_emacs_modestr_len : RL_EMACS_MODESTR_DEFLEN;
      return _rl_emacs_mode_str ? _rl_emacs_mode_str : RL_EMACS_MODESTR_DEFAULT;
    }
  else if (_rl_keymap == vi_insertion_keymap)
    {
      if (lenp)
	*lenp = _rl_vi_ins_mode_str ? _rl_vi_ins_modestr_len : RL_VI_INS_MODESTR_DEFLEN;
      return _rl_vi_ins_mode_str ? _rl_vi_ins_mode_str : RL_VI_INS_MODESTR_DEFAULT;		/* vi insert mode */
    }
  else
    {
      if (lenp)
	*lenp = _rl_vi_cmd_mode_str ? _rl_vi_cmd_modestr_len : RL_VI_CMD_MODESTR_DEFLEN;
      return _rl_vi_cmd_mode_str ? _rl_vi_cmd_mode_str : RL_VI_CMD_MODESTR_DEFAULT;		/* vi command mode */
    }
}

/* Expand the prompt string S and return the number of visible
   characters in *LP, if LP is not null.  This is currently more-or-less
   a placeholder for expansion.  LIP, if non-null is a place to store the
   index of the last invisible character in the returned string. NIFLP,
   if non-zero, is a place to store the number of invisible characters in
   the first prompt line.  The previous are used as byte counts -- indexes
   into a character buffer.  *VLP gets the number of physical characters in
   the expanded prompt (visible length) */

/* Current implementation:
	\001 (^A) start non-visible characters
	\002 (^B) end non-visible characters
   all characters except \001 and \002 (following a \001) are copied to
   the returned string; all characters except those between \001 and
   \002 are assumed to be `visible'. */	

/* Possible values for FLAGS:
	PMT_MULTILINE	caller indicates that this is part of a multiline prompt
*/

/* This approximates the number of lines the prompt will take when displayed */
#define APPROX_DIV(n, d)	(((n) < (d)) ? 1 : ((n) / (d)) + 1)

void
_rl_reset_prompt (void)
{
#ifdef TIB_TODO
  rl_visible_prompt_length = rl_expand_prompt (rl_prompt);
#endif
}

int
rl_character_len (int c, int pos)
{
  unsigned char uc;

  uc = (unsigned char)c;

  if (META_CHAR (uc))
    return (1);

  if (uc == '\t')
    {
#if defined (DISPLAY_TABS)
      return (((pos | 7) + 1) - pos);
#else
      return (2);
#endif /* !DISPLAY_TABS */
    }

  if (CTRL_CHAR (c) || c == RUBOUT)
    return (2);

  return ((ISPRINT (uc)) ? 1 : 2);
}

static void
rl_message_internal (int append, char* message)
{
#ifdef TIB_TODO
  int bneed = 0;
  int pmt_len = 0;
  char* buf = 0;
  const char* msg_color = (_rl_display_message_color && *_rl_display_message_color) ? _rl_display_message_color : _normal_color;
  const char* end_color = (_rl_display_message_color && *_rl_display_message_color) ? _normal_color : "";

  assert (message);
  if (!message)
    return;

  if (append)
    {
      pmt_len = rl_prompt ? strlen (rl_prompt) : 0;
    }
  else
    {
      const char* end = rl_prompt ? strrchr (rl_prompt, '\n') : 0;
      pmt_len = end ? (end + 1 - rl_prompt) : 0;
    }

  bneed += pmt_len;
  bneed += 1 + strlen (msg_color) + 1;
  bneed += strlen (message);
  bneed += 1 + strlen (end_color) + 1;
  ++bneed;

  buf = xmalloc (bneed);
  if (!buf)
    return;

  memcpy (buf, rl_prompt, pmt_len);
  snprintf (buf + pmt_len, bneed - pmt_len, "%c%s%c%s%c%s%c",
	RL_PROMPT_START_IGNORE, msg_color, RL_PROMPT_END_IGNORE,
	message,
	RL_PROMPT_START_IGNORE, end_color, RL_PROMPT_END_IGNORE);
  assert (strlen (buf) < bneed);

  xfree (msg_buf);
  msg_buf = buf;

  /* The Lua API clink.refilterprompt() can end up calling rl_set_prompt()
     while a message from rl_message() is active.  _rl_current_message holds
     a copy of the message so clink.refilterprompt() can reapply the current
     message. */
  xfree (_rl_current_message);
  _rl_current_message = message;
  _rl_current_message_append = append;

  rl_display_prompt = msg_buf;
  rl_expand_prompt (msg_buf);
  (*rl_redisplay_function) ();
#endif
}

static char*
make_message (const char *format, va_list args)
{
#if !defined (HAVE_VSNPRINTF)
#error vsnprintf is required.
#endif

  char* buf = 0;
#ifdef TIB_TODO
  int bneed;

  bneed = vsnprintf (0, 0, format, args);
  if (bneed < 0)
    return 0;

  ++bneed;
  buf = xmalloc (bneed);
  vsnprintf (buf, bneed, format, args);
#endif
  return buf;
}

void
rl_message (const char *format, ...)
{
#ifdef TIB_TODO
  va_list args;
  va_start (args, format);
  rl_message_internal (0, make_message(format, args));
  va_end (args);
#endif
}

void
rl_message_append (const char *format, ...)
{
#ifdef TIB_TODO
  va_list args;
  va_start (args, format);
  rl_message_internal (1, make_message(format, args));
  va_end (args);
#endif
}
/* end_clink_change */

/* How to clear things from the "echo-area". */
int
rl_clear_message (void)
{
#ifdef TIB_TODO
/* begin_clink_change */
  xfree (_rl_current_message);
  _rl_current_message = 0;
  xfree (msg_buf);
  msg_buf = 0;
/* end_clink_change */
  (*rl_redisplay_function) ();
#endif
  return 0;
}

char *
_rl_make_prompt_for_search (int pchar)
{
/* begin_clink_change */
  char *pmt = (char *)xmalloc (2);
  pmt[0] = pchar;
  pmt[1] = '\0';
/* end_clink_change */
  return pmt;
}

void
_rl_clear_screen (int clrscr)
{
#if defined (__DJGPP__)
  ScreenClear ();
  ScreenSetCursor (0, 0);
#else
  if (_rl_term_clrpag)
    {
      tputs (_rl_term_clrpag, 1, _rl_output_character_function);
      if (clrscr && _rl_term_clrscroll)
	tputs (_rl_term_clrscroll, 1, _rl_output_character_function);
    }
  else
    rl_crlf ();
#endif /* __DJGPP__ */
}
