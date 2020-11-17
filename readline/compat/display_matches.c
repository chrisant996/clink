/*

    Display matches by printing substrings, rather than a single character
    at a time.  This addresses a performance problem when printing a large
    number of

*/

#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <sys/types.h>
#include <fcntl.h>
#if defined (HAVE_SYS_FILE_H)
#  include <sys/file.h>
#endif

#include <signal.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "readline/ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <stdio.h>

#include <errno.h>
#if !defined (errno)
extern int errno;
#endif /* !errno */

#include "readline/posixdir.h"
#include "readline/posixstat.h"

/* System-specific feature definitions and include files. */
#include "readline/rldefs.h"
#include "readline/rlmbutil.h"

/* Some standard library routines. */
#include "readline/readline.h"
#include "readline/xmalloc.h"
#include "readline/rlprivate.h"

#if defined (COLOR_SUPPORT)
#  include "readline/colors.h"
#endif

#ifdef HAVE_LSTAT
#  define LSTAT lstat
#else
#  define LSTAT stat
#endif

/* Unix version of a hidden file.  Could be different on other systems. */
#define HIDDEN_FILE(fname)	((fname)[0] == '.')

#define ELLIPSIS_LEN 3

extern int complete_get_screenwidth (void);
extern int fnwidth (char *string);
extern int get_y_or_n (int for_pager);
extern char* printable_part (char* pathname);
extern int stat_char (char *filename, char match_type);
extern int _rl_internal_pager (int lines);

//------------------------------------------------------------------------------
#if defined (COLOR_SUPPORT)
static int
colored_stat_start (const char *filename, unsigned char match_type)
{
  _rl_set_normal_color ();
  return (_rl_print_color_indicator (filename, match_type));
}

static void
colored_stat_end (void)
{
  _rl_prep_non_filename_text ();
  _rl_put_indicator (&_rl_color_indicator[C_CLR_TO_EOL]);
}

static int
colored_prefix_start (void)
{
  _rl_set_normal_color ();
  return (_rl_print_prefix_color ());
}

static void
colored_prefix_end (void)
{
  colored_stat_end ();
}
#endif

//------------------------------------------------------------------------------
static int
path_isdir (const char *filename)
{
  struct stat finfo;

  return (stat (filename, &finfo) == 0 && S_ISDIR (finfo.st_mode));
}

//------------------------------------------------------------------------------
static char* grow_tmpbuf (char** tmpbuf, int* tmpsize, int needmore, int index)
{
    int oldsize = *tmpsize;
    int needsize = oldsize + needmore;
    int newsize;
    char* newbuf;

    if (!oldsize)
        oldsize = 64;
    newsize = oldsize;
    while (newsize < needsize)
        newsize *= 2;

    newbuf = (char*)xrealloc(*tmpbuf, newsize);
    *tmpbuf = newbuf;
    *tmpsize = newsize;
    return newbuf + index;
}

#define ADDCHAR(c)          do { if (i >= tmpsize) t = grow_tmpbuf(&tmpbuf, &tmpsize, 1, i); *t = (c); ++t; ++i; } while(0)
#define ADDCHARS(s, num)    do { int n = (num); if (i + n > tmpsize) t = grow_tmpbuf(&tmpbuf, &tmpsize, n, i); memcpy(t, s, n); t += n; i += n; } while(0)

//------------------------------------------------------------------------------
static int fnprint (const char *to_print, int prefix_bytes, const char *real_pathname, unsigned char match_type)
{
    static char *tmpbuf = NULL;
    static int tmpsize = 0;

    int i = 0;
    char* t = tmpbuf;

    int printed_len, w;
    const char *s;
    int common_prefix_len, print_len;
#if defined(HANDLE_MULTIBYTE)
    mbstate_t ps;
    const char *end;
    size_t tlen;
    int width;
    wchar_t wc;

    print_len = strlen(to_print);
    end = to_print + print_len + 1;
    memset(&ps, 0, sizeof(mbstate_t));
#else
    print_len = strlen(to_print);
#endif

    printed_len = common_prefix_len = 0;

    // Don't print only the ellipsis if the common prefix is one of the
    // possible completions.  Only cut off prefix_bytes if we're going to be
    // printing the ellipsis, which takes precedence over coloring the
    // completion prefix (see print_filename() below).
    if (_rl_completion_prefix_display_length > 0 && prefix_bytes >= print_len)
        prefix_bytes = 0;

#if defined(COLOR_SUPPORT)
    if (_rl_colored_stats && (prefix_bytes == 0 || _rl_colored_completion_prefix <= 0))
        colored_stat_start(real_pathname, match_type);
#endif

    if (prefix_bytes && _rl_completion_prefix_display_length > 0)
    {
        char ellipsis[ELLIPSIS_LEN + 1];

        ellipsis[0] = (to_print[prefix_bytes] == '.') ? '_' : '.';
        for (w = 1; w < ELLIPSIS_LEN; w++)
            ellipsis[w] = ellipsis[0];
        ellipsis[ELLIPSIS_LEN] = '\0';
#if defined(COLOR_SUPPORT)
        colored_prefix_start();
#endif
        fwrite(ellipsis, ELLIPSIS_LEN, 1, rl_outstream);
#if defined(COLOR_SUPPORT)
        colored_prefix_end();
        if (_rl_colored_stats)
            colored_stat_start(real_pathname, match_type); // XXX - experiment
#endif
        printed_len = ELLIPSIS_LEN;
    }
#if defined(COLOR_SUPPORT)
    else if (prefix_bytes && _rl_colored_completion_prefix > 0)
    {
        common_prefix_len = prefix_bytes;
        prefix_bytes = 0;
        // Print color indicator start here.
        colored_prefix_start();
    }
#endif

    s = to_print + prefix_bytes;
    while (*s)
    {
        if (CTRL_CHAR(*s))
        {
            ADDCHAR('^');
            ADDCHAR(UNCTRL(*s));
            printed_len += 2;
            s++;
#if defined(HANDLE_MULTIBYTE)
            memset(&ps, 0, sizeof(mbstate_t));
#endif
        }
        else if (*s == RUBOUT)
        {
            ADDCHARS("^?", 2);
            printed_len += 2;
            s++;
#if defined(HANDLE_MULTIBYTE)
            memset(&ps, 0, sizeof(mbstate_t));
#endif
        }
        else
        {
#if defined(HANDLE_MULTIBYTE)
            tlen = mbrtowc(&wc, s, end - s, &ps);
            if (MB_INVALIDCH(tlen))
            {
                tlen = 1;
                width = 1;
                memset(&ps, 0, sizeof(mbstate_t));
            }
            else if (MB_NULLWCH(tlen))
                break;
            else
            {
                w = WCWIDTH(wc);
                width = (w >= 0) ? w : 1;
            }
            ADDCHARS(s, tlen);
            s += tlen;
            printed_len += width;
#else
            ADDCHAR(*s);
            s++;
            printed_len++;
#endif
        }
        if (common_prefix_len > 0 && (s - to_print) >= common_prefix_len)
        {
            if (i)
            {
                fwrite(tmpbuf, i, 1, rl_outstream);
                i = 0;
                t = tmpbuf;
            }
#if defined(COLOR_SUPPORT)
            // printed bytes = s - to_print
            // printed bytes should never be > but check for paranoia's sake
            colored_prefix_end();
            if (_rl_colored_stats)
                colored_stat_start(real_pathname, match_type); // XXX - experiment
#endif
            common_prefix_len = 0;
        }
    }

    if (i)
        fwrite(tmpbuf, i, 1, rl_outstream);

#if defined (COLOR_SUPPORT)
    // XXX - unconditional for now.
    if (_rl_colored_stats)
        colored_stat_end();
#endif

    return printed_len;
}

// Print filename.  If VISIBLE_STATS is defined and we are using it, check for
// and output a single character for 'special' filenames.  Return the number of
// characters we output.
static int print_filename(char* to_print, char* full_pathname, int prefix_bytes)
{
    int printed_len, extension_char, slen, tlen;
    char *s, c, *new_full_pathname, *dn;
    char tmp_slash[3];

    unsigned char match_type = (rl_completion_matches_include_type ? full_pathname[0] : 0);
    int filename_display_desired = rl_filename_display_desired || IS_MATCH_TYPE_DIR(match_type);
    if (rl_completion_matches_include_type)
        full_pathname++;

    extension_char = 0;
#if defined(COLOR_SUPPORT)
    // Defer printing if we want to prefix with a color indicator.
    if (_rl_colored_stats == 0 || filename_display_desired == 0)
#endif
        printed_len = fnprint(to_print, prefix_bytes, to_print, match_type);

    if (filename_display_desired && (
#if defined (VISIBLE_STATS)
        rl_visible_stats ||
#endif
#if defined (COLOR_SUPPORT)
        _rl_colored_stats ||
#endif
        _rl_complete_mark_directories))
    {
        // If to_print != full_pathname, to_print is the basename of the path
        // passed.  In this case, we try to expand the directory name before
        // checking for the stat character.
        if (to_print != full_pathname)
        {
            // Terminate the directory name.
            c = to_print[-1];
            to_print[-1] = '\0';

            // If setting the last slash in full_pathname to a NUL results in
            // full_pathname being the empty string, we are trying to complete
            // files in the root directory.  If we pass a null string to the
            // bash directory completion hook, for example, it will expand it
            // to the current directory.  We just want the `/'.
            if (full_pathname == 0 || *full_pathname == 0)
            {
                tmp_slash[0] = rl_preferred_path_separator;
                tmp_slash[1] = '\0';
                dn = tmp_slash;
            }
            else if (!rl_is_path_separator(full_pathname[0]))
                dn = full_pathname;
            else if (full_pathname[1] == 0)
            {
                tmp_slash[0] = rl_preferred_path_separator;
                tmp_slash[1] = rl_preferred_path_separator;
                tmp_slash[2] = '\0';
                dn = tmp_slash; // restore trailing slash
            }
            else if (rl_is_path_separator(full_pathname[1]) && full_pathname[2] == 0)
            {
                tmp_slash[0] = rl_preferred_path_separator;
                tmp_slash[1] = '\0';
                dn = tmp_slash; // don't turn /// into //
            }
            else
                dn = full_pathname;
            s = tilde_expand(dn);
            if (rl_directory_completion_hook)
                (*rl_directory_completion_hook)(&s);

            slen = strlen(s);
            tlen = strlen(to_print);
            new_full_pathname = (char *)xmalloc(slen + tlen + 2);
            strcpy(new_full_pathname, s);
            if (rl_is_path_separator(s[slen - 1]))
                slen--;
            else
                new_full_pathname[slen] = rl_preferred_path_separator;
            strcpy(new_full_pathname + slen + 1, to_print);

#if defined (VISIBLE_STATS)
            if (rl_visible_stats)
                extension_char = stat_char(new_full_pathname, match_type);
            else
#endif
            if (_rl_complete_mark_directories)
            {
                dn = 0;
                if (rl_directory_completion_hook == 0 && rl_filename_stat_hook)
                {
                    dn = savestring(new_full_pathname);
                    (*rl_filename_stat_hook)(&dn);
                    xfree(new_full_pathname);
                    new_full_pathname = dn;
                }
                if (match_type <= MATCH_TYPE_NONE ? path_isdir(new_full_pathname) : IS_MATCH_TYPE_DIR(match_type))
                    extension_char = rl_preferred_path_separator;
            }

            // Move colored-stats code inside fnprint()
#if defined(COLOR_SUPPORT)
            if (_rl_colored_stats)
                printed_len = fnprint(to_print, prefix_bytes, new_full_pathname, match_type);
#endif

            xfree(new_full_pathname);
            to_print[-1] = c;
        }
        else
        {
            s = tilde_expand(full_pathname);
#if defined(VISIBLE_STATS)
            if (rl_visible_stats)
                extension_char = stat_char(s, match_type);
            else
#endif
            if (_rl_complete_mark_directories &&
                ((!match_type || IS_MATCH_TYPE_NONE(match_type)) ? path_isdir(s) : IS_MATCH_TYPE_DIR(match_type)))
                extension_char = rl_preferred_path_separator;

            // Move colored-stats code inside fnprint()
#if defined (COLOR_SUPPORT)
            if (_rl_colored_stats)
                printed_len = fnprint(to_print, prefix_bytes, s, match_type);
#endif
        }

        xfree(s);

        // Don't print a directory extension character if the filename already
        // ended with one.
        if (extension_char == rl_preferred_path_separator)
        {
            char *sep = rl_last_path_separator(to_print);
            if (sep && !sep[1])
                extension_char = 0;
        }
        if (extension_char)
        {
#if defined(COLOR_SUPPORT)
            if (extension_char == rl_preferred_path_separator)
            {
                s = tilde_expand(full_pathname);
                colored_stat_start(s, match_type);
                xfree(s);
            }
#endif
            putc(extension_char, rl_outstream);
            printed_len++;
#if defined(COLOR_SUPPORT)
            if (extension_char == rl_preferred_path_separator)
                colored_stat_end();
#endif
        }
    }

    return printed_len;
}

//------------------------------------------------------------------------------
static char* visible_part(char *match)
{
    char* t1 = printable_part(match);
    if (!rl_filename_display_desired)
        return t1;
    // check again in case of /usr/src/
    char* t2 = rl_last_path_separator(t1);
    if (!t2)
        return t1;
    return t2;
}

//------------------------------------------------------------------------------
static void pad_filename(int len, int max_spaces)
{
    int num_spaces = 0;
    if (max_spaces <= len)
        num_spaces = 1;
    else
        num_spaces = max_spaces - len;

    while (num_spaces > 0)
    {
        static const char spaces[] = "                                                ";
        const int spaces_bytes = sizeof(spaces) - sizeof(spaces[0]);
        fwrite(spaces, min(num_spaces, max_spaces), 1, rl_outstream);
        num_spaces -= max_spaces;
    }
}

//------------------------------------------------------------------------------
static int display_match_list_internal(char **matches, int len, int max, bool only_measure)
{
    int count, limit, printed_len, lines, cols;
    int i, j, k, l;
    char *temp, *t;

    // Find the length of the prefix common to all items: length as displayed
    // characters (common_length) and as a byte index into the matches (sind)
    int common_length = 0;
    int sind = 0;
    if (_rl_completion_prefix_display_length > 0)
    {
        t = visible_part(matches[0]);
        common_length = fnwidth(t);
        sind = strlen(t);
        if (common_length > max || sind > max)
            common_length = sind = 0;

        if (common_length > _rl_completion_prefix_display_length && common_length > ELLIPSIS_LEN)
            max -= common_length - ELLIPSIS_LEN;
        else
            common_length = sind = 0;
    }
#if defined(COLOR_SUPPORT)
    else if (_rl_colored_completion_prefix > 0)
    {
        t = visible_part(matches[0]);
        common_length = fnwidth(t);
        sind = RL_STRLEN(t);
        if (common_length > max || sind > max)
            common_length = sind = 0;
    }
#endif

    // How many items of MAX length can we fit in the screen window?
    cols = complete_get_screenwidth();
    max += 2;
    limit = cols / max;
    if (limit != 1 && (limit * max == cols))
        limit--;

    // Limit can end up -1 if cols == 0, or 0 if max > cols.  In that case,
    // display 1 match per iteration.
    if (limit <= 0)
        limit = 1;

    // How many iterations of the printing loop?
    count = (len + (limit - 1)) / limit;

    if (only_measure)
        return count;

    // Watch out for special case.  If LEN is less than LIMIT, then
    // just do the inner printing loop.
    //     0 < len <= limit  implies  count = 1.

    rl_crlf();

    lines = 0;
    if (_rl_print_completions_horizontally == 0)
    {
        // Print the sorted items, up-and-down alphabetically, like ls.
        for (i = 1; i <= count; i++)
        {
            for (j = 0, l = i; j < limit; j++)
            {
                if (l > len || matches[l] == 0)
                    break;
                else
                {
                    temp = printable_part(matches[l]);
                    printed_len = print_filename(temp, matches[l], sind);

                    if (j + 1 < limit)
                        pad_filename(printed_len, max);
                }
                l += count;
            }
            rl_crlf();
#if defined(SIGWINCH)
            if (RL_SIG_RECEIVED() && RL_SIGWINCH_RECEIVED() == 0)
#else
            if (RL_SIG_RECEIVED())
#endif
                return 0;
            lines++;
            if (_rl_page_completions && lines >= (_rl_screenheight - 1) && i < count)
            {
                lines = _rl_internal_pager(lines);
                if (lines < 0)
                    return 0;
            }
        }
    }
    else
    {
        // Print the sorted items, across alphabetically, like ls -x.
        for (i = 1; matches[i]; i++)
        {
            temp = printable_part(matches[i]);
            printed_len = print_filename(temp, matches[i], sind);
            // Have we reached the end of this line?
#if defined(SIGWINCH)
            if (RL_SIG_RECEIVED() && RL_SIGWINCH_RECEIVED() == 0)
#else
            if (RL_SIG_RECEIVED())
#endif
                return 0;
            if (matches[i + 1])
            {
                if (limit == 1 || (i && (limit > 1) && (i % limit) == 0))
                {
                    rl_crlf();
                    lines++;
                    if (_rl_page_completions && lines >= _rl_screenheight - 1)
                    {
                        lines = _rl_internal_pager(lines);
                        if (lines < 0)
                            return 0;
                    }
                }
                else
                    pad_filename(printed_len, max);
            }
        }
        rl_crlf();
    }

    return 0;
}

//------------------------------------------------------------------------------
void display_matches(char** matches)
{
    int len, max, i;
    char *temp;
    int vis_stat;

    // Handle simple case first.  What if there is only one answer?
    if (matches[1] == 0)
    {
        temp = printable_part(matches[0]);
        rl_crlf();
        print_filename(temp, matches[0], 0);
        rl_crlf();

        rl_forced_update_display();
        rl_display_fixed = 1;

        return;
    }

    // There is more than one answer.  Find out how many there are,
    // and find the maximum printed length of a single entry.
    for (max = 0, i = 1; matches[i]; i++)
    {
        temp = printable_part(matches[i]);
        len = fnwidth(temp);

        // If present, use the match type to determine whether there will be a
        // visible stat character, and include it in the max length calculation.
        if (rl_completion_matches_include_type)
        {
            vis_stat = -1;
            if (IS_MATCH_TYPE_DIR(matches[i][0]) && (
#if defined (VISIBLE_STATS)
                rl_visible_stats ||
#endif
#if defined (COLOR_SUPPORT)
                _rl_colored_stats ||
#endif
                _rl_complete_mark_directories))
            {
                char *sep = rl_last_path_separator(matches[i]);
                vis_stat = (!sep || sep[1]);
            }
#if defined (VISIBLE_STATS)
            else if (rl_visible_stats && rl_filename_display_desired)
                vis_stat = stat_char (matches[i] + 1, matches[i][0]);
#endif
            if (vis_stat > 0)
                len++;
        }

        if (len > max)
            max = len;
    }

    len = i - 1;

    // If there are many items, then ask the user if she really wants to
    // see them all.
    if (rl_completion_auto_query_items ?
        display_match_list_internal(matches, len, max, true) >= (_rl_screenheight - 1) :
        rl_completion_query_items > 0 && len >= rl_completion_query_items)
    {
        rl_crlf();
        if (_rl_pager_color)
            _rl_print_pager_color();
        fprintf(rl_outstream, "Display all %d possibilities? (y or n)", len);
        if (_rl_pager_color)
            fprintf(rl_outstream, "\x1b[m");
        fflush(rl_outstream);
        if (get_y_or_n(0) == 0)
        {
            rl_crlf();

            rl_forced_update_display();
            rl_display_fixed = 1;

            return;
        }
    }

    display_match_list_internal(matches, len, max, false);

    rl_forced_update_display();
    rl_display_fixed = 1;
}
