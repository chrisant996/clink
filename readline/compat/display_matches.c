/*

    Display matches by printing a line at a time, rather than a single
    character at a time.  This addresses a performance problem when printing
    a large number of matches.

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

#include "display_matches.h"

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
extern void qsort_match_list (char** matches, int len);



//------------------------------------------------------------------------------
rl_match_display_filter_func_t *rl_match_display_filter_func = NULL;
const char *_rl_filtered_color = NULL;



//------------------------------------------------------------------------------
static char* tmpbuf_allocated = NULL;
static char* tmpbuf_ptr = NULL;
static int tmpbuf_length = 0;
static int tmpbuf_capacity = 0;
static const char* const _normal_color = "\x1b[m";
static const int _normal_color_len = 3;
static const int desc_sep_padding = 4;

//------------------------------------------------------------------------------
static void reset_tmpbuf (void)
{
    tmpbuf_ptr = tmpbuf_allocated;
    tmpbuf_length = 0;
}

//------------------------------------------------------------------------------
static void grow_tmpbuf (int needsize)
{
    int oldsize = tmpbuf_capacity;
    int newsize;
    char* newbuf;

    if (!oldsize)
        oldsize = 30;
    newsize = oldsize;
    while (newsize < needsize)
        newsize *= 2;

    tmpbuf_allocated = (char*)xrealloc(tmpbuf_allocated, newsize);
    tmpbuf_ptr = tmpbuf_allocated + tmpbuf_length;
}

//------------------------------------------------------------------------------
static void append_tmpbuf_char(char c)
{
    if (tmpbuf_length + 1 > tmpbuf_capacity)
        grow_tmpbuf(tmpbuf_length + 1);

    *tmpbuf_ptr = c;
    tmpbuf_ptr++;
    tmpbuf_length++;
}

//------------------------------------------------------------------------------
static void append_tmpbuf_string(const char* s, int len)
{
    if (len < 0)
        len = strlen(s);

    if (tmpbuf_length + len > tmpbuf_capacity)
        grow_tmpbuf(tmpbuf_length + len);

    memcpy(tmpbuf_ptr, s, len);
    tmpbuf_ptr += len;
    tmpbuf_length += len;
}

//------------------------------------------------------------------------------
static void flush_tmpbuf(void)
{
    if (tmpbuf_length)
    {
        fwrite(tmpbuf_allocated, tmpbuf_length, 1, rl_outstream);
        reset_tmpbuf();
    }
}



//------------------------------------------------------------------------------
#if defined (COLOR_SUPPORT)
static void append_color_indicator(enum indicator_no colored_filetype)
{
    const struct bin_str *ind = &_rl_color_indicator[colored_filetype];
    append_tmpbuf_string(ind->string, ind->len);
}

static bool is_colored(enum indicator_no colored_filetype)
{
  size_t len = _rl_color_indicator[colored_filetype].len;
  char const *s = _rl_color_indicator[colored_filetype].string;
  return ! (len == 0
            || (len == 1 && strncmp (s, "0", 1) == 0)
            || (len == 2 && strncmp (s, "00", 2) == 0));
}

static void append_default_color(void)
{
    append_color_indicator(C_LEFT);
    append_color_indicator(C_RIGHT);
}

static void append_normal_color(void)
{
    if (is_colored(C_NORM))
    {
        append_color_indicator(C_LEFT);
        append_color_indicator(C_NORM);
        append_color_indicator(C_RIGHT);
    }
}

static void append_prefix_color(void)
{
    struct bin_str *s;

    // What do we want to use for the prefix? Let's try cyan first, see colors.h.
    s = &_rl_color_indicator[C_PREFIX];
    if (s->string != NULL)
    {
        if (is_colored(C_NORM))
            append_default_color();
        append_color_indicator(C_LEFT);
        append_tmpbuf_string(s->string, s->len);
        append_color_indicator(C_RIGHT);
    }
}

// Returns whether any color sequence was printed.
static bool append_match_color_indicator(const char *f, unsigned char match_type)
{
    enum indicator_no colored_filetype;
    COLOR_EXT_TYPE *ext; // Color extension.
    size_t len;          // Length of name.

    const char *name;
    char *filename;
    struct stat astat, linkstat;
    mode_t mode;
    int linkok; // 1 == ok, 0 == dangling symlink, -1 == missing.
    int stat_ok;

    name = f;

    // This should already have undergone tilde expansion.
    filename = 0;
    if (rl_filename_stat_hook)
    {
        filename = savestring(f);
        (*rl_filename_stat_hook)(&filename);
        name = filename;
    }

    if (match_type)
        stat_ok = stat_from_match_type(match_type, name, &astat);
    else
#if defined(HAVE_LSTAT)
        stat_ok = lstat(name, &astat);
#else
        stat_ok = stat(name, &astat);
#endif
    if (stat_ok == 0)
    {
        mode = astat.st_mode;
#if defined(HAVE_LSTAT)
        if (S_ISLNK(mode))
        {
            linkok = stat(name, &linkstat) == 0;
            if (linkok && strncmp(_rl_color_indicator[C_LINK].string, "target", 6) == 0)
                mode = linkstat.st_mode;
        }
        else
#endif
            linkok = 1;
    }
    else
        linkok = -1;

    // Is this a nonexistent file?  If so, linkok == -1.

    if (linkok == -1 && _rl_color_indicator[C_MISSING].string != NULL)
        colored_filetype = C_MISSING;
#if defined(S_ISLNK)
    else if (linkok == 0 && S_ISLNK(mode) && _rl_color_indicator[C_ORPHAN].string != NULL)
        colored_filetype = C_ORPHAN; // dangling symlink.
#endif
    else if (stat_ok != 0)
    {
        static enum indicator_no filetype_indicator[] = FILETYPE_INDICATORS;
        colored_filetype = filetype_indicator[normal]; //f->filetype];
    }
    else
    {
        if (S_ISREG(mode))
        {
            colored_filetype = C_FILE;

#if defined(S_ISUID)
            if ((mode & S_ISUID) != 0 && is_colored(C_SETUID))
                colored_filetype = C_SETUID;
            else
#endif
#if defined(S_ISGID)
                if ((mode & S_ISGID) != 0 && is_colored(C_SETGID))
                colored_filetype = C_SETGID;
            else
#endif
                if (is_colored(C_CAP) && 0) //f->has_capability)
                colored_filetype = C_CAP;
            else if ((mode & S_IXUGO) != 0 && is_colored(C_EXEC))
                colored_filetype = C_EXEC;
#if 0
            else if ((1 < astat.st_nlink) && is_colored(C_MULTIHARDLINK))
                colored_filetype = C_MULTIHARDLINK;
#endif
        }
        else if (S_ISDIR(mode))
        {
            colored_filetype = C_DIR;

#if defined (S_ISVTX)
            if ((mode & S_ISVTX) && (mode & S_IWOTH)
                && is_colored (C_STICKY_OTHER_WRITABLE))
                colored_filetype = C_STICKY_OTHER_WRITABLE;
            else
#endif
#if 0
                if ((mode & S_IWOTH) != 0 && is_colored(C_OTHER_WRITABLE))
                colored_filetype = C_OTHER_WRITABLE;
#endif
#if defined (S_ISVTX)
                else if ((mode & S_ISVTX) != 0 && is_colored(C_STICKY))
                    colored_filetype = C_STICKY;
#endif
        }
#if defined(S_ISLNK)
        else if (S_ISLNK(mode))
            colored_filetype = C_LINK;
#endif
        else if (S_ISFIFO(mode))
            colored_filetype = C_FIFO;
#if defined(S_ISSOCK)
        else if (S_ISSOCK(mode))
            colored_filetype = C_SOCK;
#endif
#if defined (S_ISBLK)
        else if (S_ISBLK(mode))
            colored_filetype = C_BLK;
#endif
        else if (S_ISCHR(mode))
            colored_filetype = C_CHR;
        else
        {
            // Classify a file of some other type as C_ORPHAN.
            colored_filetype = C_ORPHAN;
        }
    }

    if (match_type)
    {
        const char *override_color = 0;
        if (IS_MATCH_TYPE_FILE(match_type) || IS_MATCH_TYPE_DIR(match_type))
        {
            if (_rl_hidden_color && IS_MATCH_TYPE_HIDDEN(match_type))
                override_color = _rl_hidden_color;
            else if (_rl_readonly_color && IS_MATCH_TYPE_READONLY(match_type))
                override_color = _rl_readonly_color;
        }
        else if (IS_MATCH_TYPE_COMMAND(match_type))
            override_color = _rl_command_color;
        else if (IS_MATCH_TYPE_ALIAS(match_type))
            override_color = _rl_alias_color;
        if (override_color)
        {
            free(filename); // NULL or savestring return value.
            // Need to reset so not dealing with attribute combinations.
            if (is_colored(C_NORM))
                append_default_color();
            append_color_indicator(C_LEFT);
            append_tmpbuf_string(override_color, -1);
            append_color_indicator(C_RIGHT);
            return 0;
        }
    }

    // Check the file's suffix only if still classified as C_FILE.
    ext = NULL;
    if (colored_filetype == C_FILE)
    {
        // Test if NAME has a recognized suffix.
        len = strlen(name);
        name += len; // Pointer to final \0.
        for (ext = _rl_color_ext_list; ext != NULL; ext = ext->next)
        {
            if (ext->ext.len <= len && strncmp(name - ext->ext.len, ext->ext.string,
                                               ext->ext.len) == 0)
                break;
        }
    }

    free(filename); // NULL or savestring return value.

    {
        const struct bin_str *const s = ext ? &(ext->seq) : &_rl_color_indicator[colored_filetype];
        if (s->string != NULL)
        {
            // Need to reset so not dealing with attribute combinations.
            if (is_colored(C_NORM))
                append_default_color();
            append_color_indicator(C_LEFT);
            append_tmpbuf_string(s->string, s->len);
            append_color_indicator(C_RIGHT);
            return 0;
        }
        else
            return 1;
    }
}

static void prep_non_filename_text(void)
{
    if (_rl_color_indicator[C_END].string != NULL)
        append_color_indicator(C_END);
    else
    {
        append_color_indicator(C_LEFT);
        append_color_indicator(C_RESET);
        append_color_indicator(C_RIGHT);
    }
}

static void append_colored_stat_start(const char *filename, unsigned char match_type)
{
    append_normal_color();
    append_match_color_indicator(filename, match_type);
}

static void append_colored_stat_end(void)
{
    prep_non_filename_text();
}

static void append_colored_prefix_start(void)
{
    append_normal_color();
    append_prefix_color();
}

static void append_colored_prefix_end(void)
{
    append_colored_stat_end();
}
#endif

//------------------------------------------------------------------------------
static int
path_isdir(const char *filename)
{
    if (rl_completion_matches_include_type)
    {
        const char *sep = rl_last_path_separator(filename);
        return sep && !sep[1];
    }

    struct stat finfo;
    return (stat(filename, &finfo) == 0 && S_ISDIR(finfo.st_mode));
}



//------------------------------------------------------------------------------
static int fnappend(const char *to_print, int prefix_bytes, const char *real_pathname, unsigned char match_type)
{
    int printed_len, w;
    const char *s;
    int common_prefix_len, print_len;
#if defined(HANDLE_MULTIBYTE)
    mbstate_t ps;
    const char *end;
    size_t tlen;
    int width;
    WCHAR_T wc;

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
    // completion prefix (see append_filename() below).
    if (_rl_completion_prefix_display_length > 0 && prefix_bytes >= print_len)
        prefix_bytes = 0;

#if defined(COLOR_SUPPORT)
    if (_rl_colored_stats && (prefix_bytes == 0 || _rl_colored_completion_prefix <= 0))
        append_colored_stat_start(real_pathname, match_type);
#endif

    if (prefix_bytes && _rl_completion_prefix_display_length > 0)
    {
        char ellipsis = (to_print[prefix_bytes] == '.') ? '_' : '.';
#if defined(COLOR_SUPPORT)
        append_colored_prefix_start();
#endif
        for (int i = ELLIPSIS_LEN; i--;)
            append_tmpbuf_char(ellipsis);
#if defined(COLOR_SUPPORT)
        append_colored_prefix_end();
        if (_rl_colored_stats)
            append_colored_stat_start(real_pathname, match_type); // XXX - experiment
#endif
        printed_len = ELLIPSIS_LEN;
    }
#if defined(COLOR_SUPPORT)
    else if (prefix_bytes && _rl_colored_completion_prefix > 0)
    {
        common_prefix_len = prefix_bytes;
        prefix_bytes = 0;
        // Print color indicator start here.
        append_colored_prefix_start();
    }
#endif

    s = to_print + prefix_bytes;
    while (*s)
    {
        if (CTRL_CHAR(*s))
        {
            append_tmpbuf_char('^');
            append_tmpbuf_char(UNCTRL(*s));
            printed_len += 2;
            s++;
#if defined(HANDLE_MULTIBYTE)
            memset(&ps, 0, sizeof(mbstate_t));
#endif
        }
        else if (*s == RUBOUT)
        {
            append_tmpbuf_string("^?", 2);
            printed_len += 2;
            s++;
#if defined(HANDLE_MULTIBYTE)
            memset(&ps, 0, sizeof(mbstate_t));
#endif
        }
        else
        {
#if defined(HANDLE_MULTIBYTE)
            tlen = MBRTOWC(&wc, s, end - s, &ps);
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
            append_tmpbuf_string(s, tlen);
            s += tlen;
            printed_len += width;
#else
            append_tmpbuf_char(*s);
            s++;
            printed_len++;
#endif
        }
        if (common_prefix_len > 0 && (s - to_print) >= common_prefix_len)
        {
#if defined(COLOR_SUPPORT)
            // printed bytes = s - to_print
            // printed bytes should never be > but check for paranoia's sake
            append_colored_prefix_end();
            if (_rl_colored_stats)
                append_colored_stat_start(real_pathname, match_type); // XXX - experiment
#endif
            common_prefix_len = 0;
        }
    }

#if defined (COLOR_SUPPORT)
    // XXX - unconditional for now.
    if (_rl_colored_stats)
        append_colored_stat_end();
#endif

    return printed_len;
}

// Print filename.  If VISIBLE_STATS is defined and we are using it, check for
// and output a single character for 'special' filenames.  Return the number of
// characters we output.
static int append_filename(char* to_print, char* full_pathname, int prefix_bytes)
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
        printed_len = fnappend(to_print, prefix_bytes, to_print, match_type);

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

            // Move colored-stats code inside fnappend()
#if defined(COLOR_SUPPORT)
            if (_rl_colored_stats)
                printed_len = fnappend(to_print, prefix_bytes, new_full_pathname, match_type);
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

            // Move colored-stats code inside fnappend()
#if defined (COLOR_SUPPORT)
            if (_rl_colored_stats)
                printed_len = fnappend(to_print, prefix_bytes, s, match_type);
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
            if (_rl_colored_stats && extension_char == rl_preferred_path_separator)
            {
                s = tilde_expand(full_pathname);
                append_colored_stat_start(s, match_type);
                xfree(s);
            }
#endif
            append_tmpbuf_char(extension_char);
            printed_len++;
#if defined(COLOR_SUPPORT)
            if (_rl_colored_stats && extension_char == rl_preferred_path_separator)
                append_colored_stat_end();
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
    return t2 + 1;
}

//------------------------------------------------------------------------------
static void pad_filename(int len, int pad_to_width)
{
    int num_spaces = 0;
    if (pad_to_width <= len)
        num_spaces = 1;
    else
        num_spaces = pad_to_width - len;
    if (num_spaces <= 0)
        return;

#if defined(COLOR_SUPPORT)
    if (_rl_colored_stats)
        append_default_color();
#endif

    while (num_spaces > 0)
    {
        //static const char spaces[] = "................................................";
        static const char spaces[] = "                                                ";
        const int spaces_bytes = sizeof(spaces) - sizeof(spaces[0]);
        append_tmpbuf_string(spaces, min(num_spaces, spaces_bytes));
        num_spaces -= spaces_bytes;
    }
}

//------------------------------------------------------------------------------
static int display_match_list_internal(char **matches, int len, int max, bool only_measure)
{
    int count, limit, printed_len, lines, cols;
    int i, j, k, l;
    int major_stride, minor_stride;
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

    // Sort the items if they are not already sorted.
    if (rl_ignore_completion_duplicates == 0 && rl_sort_completion_matches)
        qsort_match_list(matches + 1, len);

    // Watch out for special case.  If LEN is less than LIMIT, then
    // just do the inner printing loop.
    //     0 < len <= limit  implies  count = 1.

    rl_crlf();

    if (_rl_print_completions_horizontally == 0)
    {
        // Print the sorted items, up-and-down alphabetically, like ls.
        major_stride = 1;
        minor_stride = count;
    }
    else
    {
        // Print the sorted items, across alphabetically, like ls -x.
        major_stride = limit;
        minor_stride = 1;
    }

    lines = 0;
    for (i = 0; i < count; i++)
    {
        reset_tmpbuf();
        for (j = 0, l = 1 + i * major_stride; j < limit; j++)
        {
            if (l > len || matches[l] == 0)
                break;

            temp = printable_part(matches[l]);
            printed_len = append_filename(temp, matches[l], sind);
            l += minor_stride;

            if (j + 1 < limit && l <= len && matches[l])
                pad_filename(printed_len, max);
        }
#if defined(COLOR_SUPPORT)
        if (_rl_colored_stats)
        {
            append_default_color();
            append_color_indicator(C_CLR_TO_EOL);
        }
#endif
        flush_tmpbuf();
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

    return 0;
}

//------------------------------------------------------------------------------
static int display_filtered_match_list_internal(match_display_filter_entry **matches, int len, int max, bool only_measure)
{
    int count, limit, printed_len, lines, cols;
    int i, j, l;
    int major_stride, minor_stride;
    const char* filtered_color = "\x1b[m";
    int filtered_color_len = 3;
    int show_descriptions = 0;

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

    if (matches[0] && matches[0]->visible_display < 0)
    {
        limit = 1;
        show_descriptions = 1;
    }

    // How many iterations of the printing loop?
    count = (len + (limit - 1)) / limit;

    if (only_measure)
        return count;

    // Watch out for special case.  If LEN is less than LIMIT, then
    // just do the inner printing loop.
    //     0 < len <= limit  implies  count = 1.

    rl_crlf();

    if (_rl_print_completions_horizontally == 0)
    {
        // Print the sorted items, up-and-down alphabetically, like ls.
        major_stride = 1;
        minor_stride = count;
    }
    else
    {
        // Print the sorted items, across alphabetically, like ls -x.
        major_stride = limit;
        minor_stride = 1;
    }

    if (_rl_filtered_color)
    {
        filtered_color = _rl_filtered_color;
        filtered_color_len = strlen(filtered_color);
    }

    lines = 0;
    for (i = 0; i < count; i++)
    {
        reset_tmpbuf();
        for (j = 0, l = 1 + i * major_stride; j < limit; j++)
        {
            if (l > len || matches[l] == 0)
                break;

            const match_display_filter_entry* entry = matches[l];

            printed_len = entry->visible_display;
            append_default_color();
            append_tmpbuf_string(filtered_color, filtered_color_len);
            append_tmpbuf_string(entry->display, -1);

            if (show_descriptions && matches[l]->description)
            {
                int fixed = abs(matches[0]->visible_display) + desc_sep_padding;
                pad_filename(printed_len, fixed);
                printed_len = fixed + entry->visible_description;
                append_tmpbuf_string(entry->description, -1);
            }

            l += minor_stride;

            if (j + 1 < limit && l <= len && matches[l])
                pad_filename(printed_len, max);
        }
        append_default_color();
        append_color_indicator(C_CLR_TO_EOL);
        flush_tmpbuf();
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

    return 1;
}

//------------------------------------------------------------------------------
void free_filtered_matches(match_display_filter_entry** filtered_matches)
{
    if (filtered_matches)
    {
        for (match_display_filter_entry** walk = filtered_matches; *walk; walk++)
            free(*walk);
        free(filtered_matches);
    }
}

//------------------------------------------------------------------------------
static int prompt_display_matches(int len)
{
    rl_crlf();
    if (_rl_pager_color)
        _rl_print_pager_color();
    fprintf(rl_outstream, "Display all %d possibilities? (y or n)", len);
    if (_rl_pager_color)
        fprintf(rl_outstream, _normal_color);
    fflush(rl_outstream);
    if (get_y_or_n(0) == 0)
    {
        rl_crlf();
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
void display_matches(char** matches)
{
    int len, max, max_display, max_description, i;
    char *temp;
    int vis_stat;

    // If there is a display filter, give it a chance to modify MATCHES.
    if (rl_match_display_filter_func)
    {
        match_display_filter_entry** filtered_matches = rl_match_display_filter_func(matches);
        if (filtered_matches)
        {
            if (!filtered_matches[0] || !filtered_matches[1])
            {
                rl_ding();
                free_filtered_matches(filtered_matches);
                return;
            }

            // Move to the last visible line of a possibly-multiple-line command.
            _rl_move_vert(_rl_vis_botlin);

            len = 0;
            max_display = 0;
            max_description = 0;
            for (match_display_filter_entry** walk = filtered_matches + 1; *walk; len++, walk++)
            {
                if (max_display < (*walk)->visible_display)
                    max_display = (*walk)->visible_display;
                if (max_description < (*walk)->visible_description)
                    max_description = (*walk)->visible_description;
            }

            max = max_display;
            if (max_description)
                max += desc_sep_padding + max_description;

            if (rl_completion_auto_query_items ?
                display_filtered_match_list_internal(filtered_matches, len, max, true) >= (_rl_screenheight - 1) :
                rl_completion_query_items > 0 && len >= rl_completion_query_items)
            {
                if (!prompt_display_matches(len))
                    goto done_filtered;
            }

            display_filtered_match_list_internal(filtered_matches, len, max, false);

done_filtered:
            free_filtered_matches(filtered_matches);
            goto done;
        }
    }

    // Move to the last visible line of a possibly-multiple-line command.
    _rl_move_vert(_rl_vis_botlin);

    // Handle simple case first.  What if there is only one answer?
    if (matches[1] == 0)
    {
        temp = printable_part(matches[0]);
        rl_crlf();
        reset_tmpbuf();
        append_filename(temp, matches[0], 0);
        fwrite(tmpbuf_allocated, tmpbuf_length, 1, rl_outstream);
        rl_crlf();

        goto done;
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
        if (!prompt_display_matches(len))
            goto done;
    }

    display_match_list_internal(matches, len, max, false);

done:
    rl_forced_update_display();
    rl_display_fixed = 1;
}
