/* rltty.c -- functions to prepare and restore the terminal for readline's
   use. */

/* Copyright (C) 1992-2025 Free Software Foundation, Inc.

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
#include <signal.h>
#include <errno.h>
#include <stdio.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include "rldefs.h"

#include "rltty.h"
#if defined (HAVE_SYS_IOCTL_H)
#  include <sys/ioctl.h>		/* include for declaration of ioctl */
#endif

#include "readline.h"
#include "rlprivate.h"

#if !defined (errno)
extern int errno;
#endif /* !errno */

int _rl_use_tty_xon_xoff = 1;

static void set_winsize (int);

/* **************************************************************** */
/*								    */
/*		      Saving and Restoring the TTY	    	    */
/*								    */
/* **************************************************************** */

/* Non-zero means that the terminal is in a prepped state.  There are several
   flags that are OR'd in to denote whether or not we have sent various
   init strings to the terminal. */
#define TPX_PREPPED	0x01
#define TPX_BRACKPASTE	0x02
#define TPX_METAKEY	0x04

static int terminal_prepped;

static _RL_TTY_CHARS _rl_tty_chars, _rl_last_tty_chars;

/* If non-zero, means that this process has called tcflow(fd, TCOOFF)
   and output is suspended. */
#if defined (__ksr1__)
static int ksrflow;
#endif

/* Dummy call to force a backgrounded readline to stop before it tries
   to get the tty settings. But we use the information to set our idea
   of the screen size if we're in a signal handling context, since it
   doesn't make sense to waste it. */
static void
set_winsize (int tty)
{
#if defined (TIOCGWINSZ) || defined (HAVE_TCGETWINSIZE)
  struct winsize w;
  
  if (_rl_tcgetwinsize (tty, &w) == 0)
    {
      (void) _rl_tcsetwinsize (tty, &w);
      /* We restrict this to the case where we're running a signal handler
	 and executing after a SIGTSTP. We can relax it later. */
#if defined (SIGTSTP)
      if (RL_ISSTATE (RL_STATE_SIGHANDLER) && _rl_handling_signal == SIGTSTP && rl_prefer_env_winsize == 0)
	_rl_set_screen_size (w.ws_row, w.ws_col);	/* don't waste the info */
#endif
    }
#endif /* TIOCGWINSZ */
}

/* **************************************************************** */
/*								    */
/*			Bogus Flow Control      		    */
/*								    */
/* **************************************************************** */

int
rl_restart_output (int count, int key)
{
#if defined (__MINGW32__)
  return 0;
#else /* !__MING32__ */

  int fildes = fileno (rl_outstream);
#if defined (TIOCSTART)
#if defined (apollo)
  ioctl (&fildes, TIOCSTART, 0);
#else
  ioctl (fildes, TIOCSTART, 0);
#endif /* apollo */

#else /* !TIOCSTART */
#  if defined (TERMIOS_TTY_DRIVER)
#    if defined (__ksr1__)
  if (ksrflow)
    {
      ksrflow = 0;
      tcflow (fildes, TCOON);
    }
#    else /* !ksr1 */
  tcflow (fildes, TCOON);		/* Simulate a ^Q. */
#    endif /* !ksr1 */
#  else /* !TERMIOS_TTY_DRIVER */
#    if defined (TCXONC)
  ioctl (fildes, TCXONC, TCOON);
#    endif /* TCXONC */
#  endif /* !TERMIOS_TTY_DRIVER */
#endif /* !TIOCSTART */

  return 0;
#endif /* !__MINGW32__ */
}

int
rl_stop_output (int count, int key)
{
#if defined (__MINGW32__)
  return 0;
#else

  int fildes = fileno (rl_instream);

#if defined (TIOCSTOP)
# if defined (apollo)
  ioctl (&fildes, TIOCSTOP, 0);
# else
  ioctl (fildes, TIOCSTOP, 0);
# endif /* apollo */
#else /* !TIOCSTOP */
# if defined (TERMIOS_TTY_DRIVER)
#  if defined (__ksr1__)
  ksrflow = 1;
#  endif /* ksr1 */
  tcflow (fildes, TCOOFF);
# else
#   if defined (TCXONC)
  ioctl (fildes, TCXONC, TCOON);
#   endif /* TCXONC */
# endif /* !TERMIOS_TTY_DRIVER */
#endif /* !TIOCSTOP */

  return 0;
#endif /* !__MINGW32__ */
}

/* **************************************************************** */
/*								    */
/*			Default Key Bindings			    */
/*								    */
/* **************************************************************** */

#if !defined (NO_TTY_DRIVER)
#define SET_SPECIAL(sc, func)	set_special_char(kmap, &ttybuff, sc, func)
#endif

#if defined (NO_TTY_DRIVER)

#define SET_SPECIAL(sc, func)
#define RESET_SPECIAL(c)

#elif defined (NEW_TTY_DRIVER)
static void
set_special_char (Keymap kmap, TIOTYPE *tiop, int sc, rl_command_func_t *func)
{
  if (sc != -1 && kmap[(unsigned char)sc].type == ISFUNC)
    kmap[(unsigned char)sc].function = func;
}

#define RESET_SPECIAL(c) \
  if (c != -1 && kmap[(unsigned char)c].type == ISFUNC) \
    kmap[(unsigned char)c].function = rl_insert;

static void
_rl_bind_tty_special_chars (Keymap kmap, TIOTYPE ttybuff)
{
  if (ttybuff.flags & SGTTY_SET)
    {
      SET_SPECIAL (ttybuff.sgttyb.sg_erase, rl_rubout);
      SET_SPECIAL (ttybuff.sgttyb.sg_kill, rl_unix_line_discard);
    }

#  if defined (TIOCGLTC)
  if (ttybuff.flags & LTCHARS_SET)
    {
      SET_SPECIAL (ttybuff.ltchars.t_werasc, rl_unix_word_rubout);
      SET_SPECIAL (ttybuff.ltchars.t_lnextc, rl_quoted_insert);
    }
#  endif /* TIOCGLTC */
}

#else /* !NEW_TTY_DRIVER */
static void
set_special_char (Keymap kmap, TIOTYPE *tiop, int sc, rl_command_func_t *func)
{
  unsigned char uc;

  uc = tiop->c_cc[sc];
  if (uc != (unsigned char)_POSIX_VDISABLE && kmap[uc].type == ISFUNC)
    kmap[uc].function = func;
}

/* used later */
#define RESET_SPECIAL(uc) \
  if (uc != (unsigned char)_POSIX_VDISABLE && kmap[uc].type == ISFUNC) \
    kmap[uc].function = rl_insert;

static void
_rl_bind_tty_special_chars (Keymap kmap, TIOTYPE ttybuff)
{
  SET_SPECIAL (VERASE, rl_rubout);
  SET_SPECIAL (VKILL, rl_unix_line_discard);

#  if defined (VLNEXT) && defined (TERMIOS_TTY_DRIVER)
  SET_SPECIAL (VLNEXT, rl_quoted_insert);
#  endif /* VLNEXT && TERMIOS_TTY_DRIVER */

#  if defined (VWERASE) && defined (TERMIOS_TTY_DRIVER)
#    if defined (VI_MODE)
  if (rl_editing_mode == vi_mode)
    SET_SPECIAL (VWERASE, rl_vi_unix_word_rubout);
  else
#    endif
  SET_SPECIAL (VWERASE, rl_unix_word_rubout);
#  endif /* VWERASE && TERMIOS_TTY_DRIVER */
}

#endif /* !NEW_TTY_DRIVER */

/* Set the system's default editing characters to their readline equivalents
   in KMAP.  Should be static, now that we have rl_tty_set_default_bindings. */
void
rltty_set_default_bindings (Keymap kmap)
{
#if !defined (NO_TTY_DRIVER)
  TIOTYPE ttybuff;
  int tty;

  tty = fileno (rl_instream);

  if (get_tty_settings (tty, &ttybuff) == 0)
    _rl_bind_tty_special_chars (kmap, ttybuff);
#endif
}

/* New public way to set the system default editing chars to their readline
   equivalents. */
void
rl_tty_set_default_bindings (Keymap kmap)
{
  rltty_set_default_bindings (kmap);
}

/* Rebind all of the tty special chars that readline worries about back
   to self-insert.  Call this before saving the current terminal special
   chars with save_tty_chars().  This only works on POSIX termios or termio
   systems. */
void
rl_tty_unset_default_bindings (Keymap kmap)
{
  /* Don't bother before we've saved the tty special chars at least once. */
  if (RL_ISSTATE(RL_STATE_TTYCSAVED) == 0)
    return;

  RESET_SPECIAL (_rl_tty_chars.t_erase);
  RESET_SPECIAL (_rl_tty_chars.t_kill);

#  if defined (VLNEXT) && defined (TERMIOS_TTY_DRIVER)
  RESET_SPECIAL (_rl_tty_chars.t_lnext);
#  endif /* VLNEXT && TERMIOS_TTY_DRIVER */

#  if defined (VWERASE) && defined (TERMIOS_TTY_DRIVER)
  RESET_SPECIAL (_rl_tty_chars.t_werase);
#  endif /* VWERASE && TERMIOS_TTY_DRIVER */
}

#if defined (HANDLE_SIGNALS)

#if defined (NEW_TTY_DRIVER) || defined (NO_TTY_DRIVER)
int
_rl_disable_tty_signals (void)
{
  return 0;
}

int
_rl_restore_tty_signals (void)
{
  return 0;
}
#else

static TIOTYPE sigstty, nosigstty;
static int tty_sigs_disabled = 0;

int
_rl_disable_tty_signals (void)
{
  if (tty_sigs_disabled)
    return 0;

  if (_get_tty_settings (fileno (rl_instream), &sigstty) < 0)
    return -1;

  nosigstty = sigstty;

  nosigstty.c_lflag &= ~ISIG;
  nosigstty.c_iflag &= ~IXON;

  if (_set_tty_settings (fileno (rl_instream), &nosigstty) < 0)
    return (_set_tty_settings (fileno (rl_instream), &sigstty));

  tty_sigs_disabled = 1;
  return 0;
}

int
_rl_restore_tty_signals (void)
{
  int r;

  if (tty_sigs_disabled == 0)
    return 0;

  r = _set_tty_settings (fileno (rl_instream), &sigstty);

  if (r == 0)
    tty_sigs_disabled = 0;

  return r;
}
#endif /* !NEW_TTY_DRIVER */

#endif /* HANDLE_SIGNALS */
