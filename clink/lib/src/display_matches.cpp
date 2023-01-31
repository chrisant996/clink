/*

    Display matches by printing a line at a time, rather than a single
    character at a time.  This addresses a performance problem when printing
    a large number of matches.

*/

#include "pch.h"
#include <assert.h>

#define READLINE_LIBRARY
#define BUILD_READLINE

#include "display_matches.h"
#include "matches_lookaside.h"
#include "match_adapter.h"
#include "column_widths.h"
#include "ellipsify.h"
#include "line_buffer.h"

#include <core/base.h>
#include <core/settings.h>
#include <terminal/ecma48_iter.h>

extern "C" {

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

int __complete_get_screenwidth (void);
int __fnwidth (const char *string);
int __get_y_or_n (int for_pager);
char* __printable_part (char* pathname);
int __stat_char (const char *filename, char match_type);
int ___rl_internal_pager (int lines);
unsigned int cell_count(const char* in);

} // extern "C"

#ifdef HAVE_LSTAT
#  define LSTAT lstat
#else
#  define LSTAT stat
#endif

/* Unix version of a hidden file.  Could be different on other systems. */
#define HIDDEN_FILE(fname)	((fname)[0] == '.')

#define ELLIPSIS_LEN ellipsis_len



//------------------------------------------------------------------------------
extern setting_bool g_match_best_fit;
extern setting_int g_match_limit_fitted;
extern line_buffer* g_rl_buffer;
rl_match_display_filter_func_t *rl_match_display_filter_func = nullptr;
const char *_rl_description_color = nullptr;
const char *_rl_filtered_color = nullptr;
const char *_rl_arginfo_color = nullptr;
const char *_rl_selected_color = nullptr;



//------------------------------------------------------------------------------
static char* tmpbuf_allocated = nullptr;
static char* tmpbuf_ptr = nullptr;
static int tmpbuf_length = 0;
static int tmpbuf_capacity = 0;
static int tmpbuf_rollback_length = 0;
static const char* const _normal_color = "\x1b[m";
static const int _normal_color_len = 3;

//------------------------------------------------------------------------------
void mark_tmpbuf (void)
{
    tmpbuf_rollback_length = tmpbuf_length;
}

//------------------------------------------------------------------------------
void reset_tmpbuf (void)
{
    tmpbuf_ptr = tmpbuf_allocated;
    tmpbuf_length = 0;
    mark_tmpbuf();
}

//------------------------------------------------------------------------------
void rollback_tmpbuf (void)
{
    tmpbuf_ptr = tmpbuf_allocated + tmpbuf_rollback_length;
    tmpbuf_length = tmpbuf_rollback_length;
}

//------------------------------------------------------------------------------
static void grow_tmpbuf (int growby)
{
    int needsize = tmpbuf_length + growby + 1;
    if (needsize <= tmpbuf_capacity)
        return;

    int oldsize = tmpbuf_capacity;
    int newsize;
    char* newbuf;

    if (!oldsize)
        oldsize = 30;
    newsize = oldsize;
    while (newsize < needsize)
        newsize *= 2;

    tmpbuf_allocated = (char*)xrealloc(tmpbuf_allocated, newsize);
    tmpbuf_capacity = newsize;
    tmpbuf_ptr = tmpbuf_allocated + tmpbuf_length;
}

//------------------------------------------------------------------------------
void append_tmpbuf_char(char c)
{
    grow_tmpbuf(1);

    *tmpbuf_ptr = c;
    tmpbuf_ptr++;
    tmpbuf_length++;
}

//------------------------------------------------------------------------------
void append_tmpbuf_string(const char* s, int len)
{
    if (len < 0)
        len = strlen(s);

    grow_tmpbuf(len);

    memcpy(tmpbuf_ptr, s, len);
    tmpbuf_ptr += len;
    tmpbuf_length += len;
}

//------------------------------------------------------------------------------
const char* get_tmpbuf_rollback (void)
{
    grow_tmpbuf(1);
    *tmpbuf_ptr = '\0';
    return tmpbuf_allocated + tmpbuf_rollback_length;
}

//------------------------------------------------------------------------------
void flush_tmpbuf(void)
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
    if (s->string != nullptr)
    {
        if (is_colored(C_NORM))
            append_default_color();
        append_color_indicator(C_LEFT);
        append_tmpbuf_string(s->string, s->len);
        append_color_indicator(C_RIGHT);
    }
}

// Returns whether any color sequence was printed.
static bool append_match_color_indicator(const char *f, match_type type)
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

    if (!is_zero(type))
#if defined(HAVE_LSTAT)
        stat_ok = stat_from_match_type(static_cast<match_type_intrinsic>(type), name, &astat, &linkstat);
#else
        stat_ok = stat_from_match_type(static_cast<match_type_intrinsic>(type), name, &astat);
#endif
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
            if (!is_zero(type))
                linkok = linkstat.st_mode != 0;
            else
                linkok = stat(name, &linkstat) == 0;
            if (linkok && _strnicmp(_rl_color_indicator[C_LINK].string, "target", 6) == 0)
                mode = linkstat.st_mode;
        }
        else
#endif
            linkok = 1;
    }
    else
        linkok = -1;

    // Is this a nonexistent file?  If so, linkok == -1.

    if (linkok == -1 && _rl_color_indicator[C_MISSING].string != nullptr)
        colored_filetype = C_MISSING;
#if defined(S_ISLNK)
    else if (linkok == 0 && S_ISLNK(mode) && _rl_color_indicator[C_ORPHAN].string != nullptr)
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
            else if ((1 < astat.st_nlink) && is_colored(C_MULTIHARDLINK))
                colored_filetype = C_MULTIHARDLINK;
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
                if ((mode & S_IWOTH) != 0 && is_colored(C_OTHER_WRITABLE))
                colored_filetype = C_OTHER_WRITABLE;
#if defined (S_ISVTX)
                else if ((mode & S_ISVTX) != 0 && is_colored(C_STICKY))
                    colored_filetype = C_STICKY;
#endif
        }
#if defined(S_ISLNK)
        else if (S_ISLNK(mode) && _strnicmp(_rl_color_indicator[C_LINK].string, "target", 6) != 0)
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

    if (!is_zero(type))
    {
        const char *override_color = 0;
        if (is_pathish(type))
        {
            if (_rl_hidden_color && is_match_type_hidden(type))
                override_color = _rl_hidden_color;
            else if (_rl_readonly_color && is_match_type_readonly(type))
                override_color = _rl_readonly_color;
        }
        else if (is_match_type(type, match_type::cmd))
        {
            override_color = _rl_command_color;
            colored_filetype = C_NORM;
        }
        else if (is_match_type(type, match_type::alias))
        {
            override_color = _rl_alias_color;
            colored_filetype = C_NORM;
        }
        else
            colored_filetype = C_NORM;
        if (override_color)
        {
            free(filename); // nullptr or savestring return value.
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
    ext = nullptr;
    if (colored_filetype == C_FILE)
    {
        // Test if NAME has a recognized suffix.
        len = strlen(name);
        name += len; // Pointer to final \0.
        for (ext = _rl_color_ext_list; ext != nullptr; ext = ext->next)
        {
            if (ext->ext.len <= len && _strnicmp(name - ext->ext.len, ext->ext.string,
                                                 ext->ext.len) == 0)
                break;
        }
    }

    free(filename); // nullptr or savestring return value.

    {
        const struct bin_str *const s = ext ? &(ext->seq) : &_rl_color_indicator[colored_filetype];
        if (s->string != nullptr)
        {
            // Need to reset so not dealing with attribute combinations.
            if (is_colored(C_NORM))
                append_default_color();
            append_color_indicator(C_LEFT);
            append_tmpbuf_string(s->string, s->len);
            append_color_indicator(C_RIGHT);
            return (!ext && colored_filetype == C_FILE) ? 1 : 0;
        }
        else
            return 1;
    }
}

static void prep_non_filename_text(void)
{
    if (_rl_color_indicator[C_END].string != nullptr)
        append_color_indicator(C_END);
    else
    {
        append_color_indicator(C_LEFT);
        append_color_indicator(C_RESET);
        append_color_indicator(C_RIGHT);
    }
}

static void append_colored_stat_start(const char *filename, match_type type)
{
    append_normal_color();
    append_match_color_indicator(filename, type);
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

static void append_selection_color(void)
{
    if (is_colored(C_NORM))
        append_default_color();
    append_color_indicator(C_LEFT);
    append_tmpbuf_string(_rl_selected_color ? _rl_selected_color : "7", -1);
    append_color_indicator(C_RIGHT);
}
#endif

//------------------------------------------------------------------------------
static int
path_isdir(match_type type, const char *filename)
{
    if (!is_zero(type) && !is_match_type(type, match_type::none))
        return is_match_type(type, match_type::dir);

    struct stat finfo;
    return (stat(filename, &finfo) == 0 && S_ISDIR(finfo.st_mode));
}



//------------------------------------------------------------------------------
static int fnappend(const char *to_print, int prefix_bytes, int condense, const char *real_pathname, match_type match_type, int selected)
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
    if (condense && prefix_bytes >= print_len)
        prefix_bytes = 0;

    if (selected)
        append_selection_color();
#if defined(COLOR_SUPPORT)
    else if (_rl_colored_stats && (prefix_bytes == 0 || _rl_colored_completion_prefix <= 0))
        append_colored_stat_start(real_pathname, match_type);
#endif

    if (prefix_bytes && condense)
    {
        char ellipsis = (to_print[prefix_bytes] == '.') ? '_' : '.';
#if defined(COLOR_SUPPORT)
        if (!selected)
            append_colored_prefix_start();
#endif
        for (int i = ELLIPSIS_LEN; i--;)
            append_tmpbuf_char(ellipsis);
#if defined(COLOR_SUPPORT)
        if (!selected)
        {
            append_colored_prefix_end();
            if (_rl_colored_stats)
                append_colored_stat_start(real_pathname, match_type); // XXX - experiment
        }
#endif
        printed_len = ELLIPSIS_LEN;
    }
#if defined(COLOR_SUPPORT)
    else if (prefix_bytes && _rl_colored_completion_prefix > 0)
    {
        common_prefix_len = prefix_bytes;
        prefix_bytes = 0;
        // Print color indicator start here.
        if (!selected)
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
            if (!selected)
            {
                append_colored_prefix_end();
                if (_rl_colored_stats)
                    append_colored_stat_start(real_pathname, match_type); // XXX - experiment
            }
#endif
            common_prefix_len = 0;
        }
    }

#if defined (COLOR_SUPPORT)
    // XXX - unconditional for now.
    if (_rl_colored_stats && !selected)
        append_colored_stat_end();
#endif

    return printed_len;
}

//------------------------------------------------------------------------------
void append_display(const char* to_print, int selected, const char* color)
{
    if (selected)
    {
        append_selection_color();
        if (color)
        {
            while (*to_print == ' ')
            {
                append_tmpbuf_string(to_print, 1);
                to_print++;
            }

            str<> tmp;
            ecma48_processor(color, &tmp, nullptr, ecma48_processor_flags::colorless);
            append_tmpbuf_string(tmp.c_str(), tmp.length());
        }
    }
    else
    {
        append_default_color();
        if (color)
        {
            while (*to_print == ' ')
            {
                append_tmpbuf_string(to_print, 1);
                to_print++;
            }

            append_tmpbuf_string(color, -1);
        }
    }

    append_tmpbuf_string(to_print, -1);

    if (!selected)
        append_default_color();
}

//------------------------------------------------------------------------------
// Print filename.  If VISIBLE_STATS is defined and we are using it, check for
// and output a single character for 'special' filenames.  Return the number of
// characters we output.
// The optional VIS_STAT_CHAR receives the visual stat char.  This is to allow
// the visual stat char to show up correctly even after an ellipsis.
int append_filename(char* to_print, const char* full_pathname, int prefix_bytes, int condense, match_type type, int selected, int* vis_stat_char)
{
    int printed_len, extension_char, slen, tlen;
    char *s, c, *new_full_pathname;
    const char *dn;
    char tmp_slash[3];

    int filename_display_desired = rl_filename_display_desired || is_match_type(type, match_type::dir);

    extension_char = 0;
#if defined(COLOR_SUPPORT)
    // Defer printing if we want to prefix with a color indicator.
    if (_rl_colored_stats == 0 || filename_display_desired == 0)
#endif
        printed_len = fnappend(to_print, prefix_bytes, condense, to_print, type, selected);

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
            // BUGBUG: path::tilde_expand() would behave more correctly.
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
                extension_char = __stat_char(new_full_pathname, static_cast<match_type_intrinsic>(type));
            else
#endif
            if (_rl_complete_mark_directories)
            {
                if (rl_directory_completion_hook == 0 && rl_filename_stat_hook)
                {
                    char *tmp = savestring(new_full_pathname);
                    (*rl_filename_stat_hook)(&tmp);
                    xfree(new_full_pathname);
                    new_full_pathname = tmp;
                }
                if (path_isdir(type, new_full_pathname))
                    extension_char = rl_preferred_path_separator;
            }

            // Move colored-stats code inside fnappend()
#if defined(COLOR_SUPPORT)
            if (_rl_colored_stats)
                printed_len = fnappend(to_print, prefix_bytes, condense, new_full_pathname, type, selected);
#endif

            xfree(new_full_pathname);
            to_print[-1] = c;
        }
        else
        {
            // BUGBUG: path::tilde_expand() would behave more correctly.
            s = tilde_expand(full_pathname);
#if defined(VISIBLE_STATS)
            if (rl_visible_stats)
                extension_char = __stat_char(s, static_cast<match_type_intrinsic>(type));
            else
#endif
            if (_rl_complete_mark_directories && path_isdir(type, s))
                extension_char = rl_preferred_path_separator;

            // Move colored-stats code inside fnappend()
#if defined (COLOR_SUPPORT)
            if (_rl_colored_stats)
                printed_len = fnappend(to_print, prefix_bytes, condense, s, type, selected);
#endif
        }

        xfree(s);

        // Don't print a directory extension character if the filename already
        // ended with one.
        if (extension_char == rl_preferred_path_separator)
        {
            char *sep = rl_last_path_separator(to_print);
            if (sep && !sep[1])
            {
                if (vis_stat_char)
                    *vis_stat_char = extension_char;
                extension_char = 0;
            }
        }
        if (extension_char)
        {
            if (vis_stat_char)
                *vis_stat_char = extension_char;
#if defined(COLOR_SUPPORT)
            if (_rl_colored_stats && extension_char == rl_preferred_path_separator)
            {
                // BUGBUG: path::tilde_expand() would behave more correctly.
                s = tilde_expand(full_pathname);
                if (!selected)
                    append_colored_stat_start(s, type);
                xfree(s);
            }
#endif
            append_tmpbuf_char(extension_char);
            printed_len++;
#if defined(COLOR_SUPPORT)
            if (_rl_colored_stats && !selected && extension_char == rl_preferred_path_separator)
                append_colored_stat_end();
#endif
        }
    }

    return printed_len;
}

//------------------------------------------------------------------------------
// SELECTED > 0     : pad exactly, reset color at end.
// SELECTED == 0    : reset color first, pad at least 1 char.
// SELECTED == -1   : pad exactly, do not set color.
void pad_filename(int len, int pad_to_width, int selected)
{
    const bool exact = selected || pad_to_width < 0;
    int num_spaces = 0;
    if (pad_to_width < 0)
        pad_to_width = -pad_to_width;
    if (pad_to_width <= len)
        num_spaces = exact ? 0 : 1;
    else
        num_spaces = pad_to_width - len;

#if defined(COLOR_SUPPORT)
    if (_rl_colored_stats && selected == 0)
        append_default_color();
#endif

    while (num_spaces > 0)
    {
        //static const char spaces[] = "................................................";
        static const char spaces[] = "                                                ";
        const int spaces_bytes = sizeof(spaces) - sizeof(spaces[0]);
        const int chunk_len = (num_spaces < spaces_bytes) ? num_spaces : spaces_bytes;
        append_tmpbuf_string(spaces, chunk_len);
        num_spaces -= spaces_bytes;
    }

    if (selected > 0)
        append_default_color();
}

//------------------------------------------------------------------------------
bool get_match_color(const char* filename, match_type type, str_base& out)
{
    reset_tmpbuf();
    const bool has = !append_match_color_indicator(filename, type);
    out = get_tmpbuf_rollback();
    return has;
}



//------------------------------------------------------------------------------
inline bool exact_match(const char* match, const char* text, unsigned int len)
{
    while (len--)
    {
        if (*match != *text)
            return false;
        ++match;
        ++text;
    }
    return !*match;
}

//------------------------------------------------------------------------------
enum { bit_prefix = 0x01, bit_suffix = 0x02 };
static int calc_prefix_or_suffix(const char* match, const char* display)
{
    const unsigned int match_len = static_cast<unsigned int>(strlen(match));

    bool pre = false;
    bool suf = false;
    int visible_len = 0;

    ecma48_state state;
    ecma48_iter iter(display, state);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            assert(code.get_length());
            suf = false;

            if (match_len <= code.get_length())
            {
                if (!visible_len)
                    pre = exact_match(match, code.get_pointer(), code.get_length());
                suf = exact_match(match, code.get_pointer() + code.get_length() - match_len, match_len);
            }

            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (int c = inner_iter.next())
                visible_len += clink_wcwidth(c);
        }

    return (pre ? bit_prefix : 0) + (suf ? bit_suffix : 0);
}

//------------------------------------------------------------------------------
int append_tmpbuf_with_visible_len(const char* s, int len)
{
    append_tmpbuf_string(s, len);

    int visible_len = 0;

    ecma48_state state;
    ecma48_iter iter(s, state, len);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (int c = inner_iter.next())
                visible_len += clink_wcwidth(c);
        }

    return visible_len;
}

//------------------------------------------------------------------------------
static int append_prefix_complex(const char* text, int len, const char* leading, int prefix_bytes, int condense)
{
    int visible_len = 0;

    // Prefix.
    append_colored_prefix_start();
    if (condense && prefix_bytes < len)
    {
        const char ellipsis = (text[prefix_bytes] == '.') ? '_' : '.';
        for (int i = ELLIPSIS_LEN; i--;)
            append_tmpbuf_char(ellipsis);
        visible_len += ELLIPSIS_LEN;
    }
    else
    {
        visible_len += append_tmpbuf_with_visible_len(text, prefix_bytes);
    }

    // Restore color.
    append_default_color();
    if (_rl_filtered_color)
        append_tmpbuf_string(_rl_filtered_color, -1);
    append_tmpbuf_string(leading, -1);

    // Rest of text.
    visible_len += append_tmpbuf_with_visible_len(text + prefix_bytes, len - prefix_bytes);

    return visible_len;

}



//------------------------------------------------------------------------------
unsigned int append_display_with_presuf(const char* match, const char* display, int presuf, int prefix_bytes, int condense, match_type type)
{
    assert(presuf);
    bool pre = !!(presuf & bit_prefix);
    bool suf = !pre && (presuf & bit_suffix);
    const int skip_cells = suf ? cell_count(display) - cell_count(match) : 0;

    int visible_len = 0;
    bool reset_default_color = false;
    str<> leading;

    ecma48_state state;
    ecma48_iter iter(display, state);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            assert(code.get_length());

            if (pre)
            {
                pre = false;
                assert(!suf);
                if (prefix_bytes <= code.get_length())
                {
                    visible_len += append_prefix_complex(code.get_pointer(), code.get_length(), leading.c_str(), prefix_bytes, condense);
                    continue;
                }
            }

            bool did = false;
            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (true)
            {
                if (suf && visible_len == skip_cells)
                {
                    const unsigned int rest_len = inner_iter.length();
                    if (prefix_bytes <= rest_len)
                    {
                        suf = false;
                        did = true;
                        // Already counted in visible_len.
                        append_tmpbuf_string(code.get_pointer(), static_cast<unsigned int>(inner_iter.get_pointer() - code.get_pointer()));
                        visible_len += append_prefix_complex(inner_iter.get_pointer(), rest_len, leading.c_str(), prefix_bytes, condense);
                        break;
                    }
                }

                const int c = inner_iter.next();
                if (!c)
                    break;
                visible_len += clink_wcwidth(c);
            }

            if (!did)
                append_tmpbuf_string(code.get_pointer(), code.get_length());
        }
        else
        {
            append_tmpbuf_string(code.get_pointer(), code.get_length());
            leading.concat(code.get_pointer(), code.get_length());
            reset_default_color = true;
        }

    if (reset_default_color)
        append_tmpbuf_string(_normal_color, _normal_color_len);

    return visible_len;
}



//------------------------------------------------------------------------------
static int display_match_list_internal(match_adapter* adapter, const column_widths& widths, bool only_measure, int presuf)
{
    const int count = adapter->get_match_count();
    int printed_len;
    const char* filtered_color = "\x1b[m";
    const char* description_color = "\x1b[m";
    int filtered_color_len = 3;
    int description_color_len = 3;

    const int cols = __complete_get_screenwidth();
    const int show_descriptions = adapter->has_descriptions();
    const int limit = widths.num_columns();
    assert(limit > 0);

    // How many iterations of the printing loop?
    const int rows = (count + (limit - 1)) / limit;

    // If only measuring, short circuit without printing anything.
    if (only_measure)
        return rows;

    // Give the transient prompt a chance to update before printing anything.
    end_prompt(1/*crlf*/);

    // Watch out for special case.  If COUNT is less than LIMIT, then
    // just do the inner printing loop.
    //     0 < count <= limit  implies  rows = 1.

    // Horizontally means across alphabetically, like ls -x.
    // Vertically means up-and-down alphabetically, like ls.
    const int major_stride = _rl_print_completions_horizontally ? limit : 1;
    const int minor_stride = _rl_print_completions_horizontally ? 1 : rows;

    if (_rl_filtered_color)
    {
        filtered_color = _rl_filtered_color;
        filtered_color_len = strlen(filtered_color);
    }

    if (_rl_description_color)
    {
        description_color = _rl_description_color;
        description_color_len = strlen(description_color);
    }

    int lines = 0;
    for (int i = 0; i < rows; i++)
    {
        reset_tmpbuf();
        for (int j = 0, l = i * major_stride; j < limit; j++)
        {
            if (l >= count)
                break;

            const int col_max = ((show_descriptions && !widths.m_right_justify) ?
                                 cols - 1 :
                                 widths.column_width(j));

            const match_type type = adapter->get_match_type(l);
            const char* const match = adapter->get_match(l);
            const char* const display = adapter->get_match_display(l);
            const bool append = adapter->is_append_display(l);

            if (adapter->use_display(l, type, append))
            {
                printed_len = 0;
                if (append)
                {
                    char* temp = __printable_part((char*)match);
                    printed_len = append_filename(temp, match, widths.m_sind, widths.m_can_condense, type, 0, nullptr);
                    append_display(display, 0, _rl_arginfo_color);
                    printed_len += adapter->get_match_visible_display(l);
                }
                else if (presuf)
                {
                    printed_len += append_display_with_presuf(match, display, presuf, widths.m_sind, widths.m_can_condense, type);
                }
                else
                {
                    append_display(display, 0, _rl_filtered_color);
                    printed_len += adapter->get_match_visible_display(l);
                }
            }
            else
            {
                char* temp = __printable_part((char*)display);
                printed_len = append_filename(temp, display, widths.m_sind, widths.m_can_condense, type, 0, nullptr);
            }

            if (show_descriptions)
            {
                const char* const description = adapter->get_match_description(l);
                if (description && *description)
                {
                    const bool right_justify = widths.m_right_justify;
#ifdef USE_DESC_PARENS
                    const int parens = right_justify ? 2 : 0;
#else
                    const int parens = 0;
#endif
                    const int pad_to = (right_justify ?
                        max<int>(printed_len + widths.m_desc_padding, col_max - (adapter->get_match_visible_description(l) + parens)) :
                        widths.m_max_match + 4);
                    if (pad_to < cols - 1)
                    {
                        pad_filename(printed_len, pad_to, 0);
                        printed_len = pad_to + parens;
                        append_tmpbuf_string(description_color, description_color_len);
                        if (parens)
                            append_tmpbuf_string("(", 1);
                        printed_len += ellipsify_to_callback(description, col_max - printed_len, false/*expand_ctrl*/, append_tmpbuf_string);
                        if (parens)
                            append_tmpbuf_string(")", 1);
                        append_tmpbuf_string(_normal_color, _normal_color_len);
                    }
                }
            }

            l += minor_stride;

            if (j + 1 < limit && l < count)
                pad_filename(printed_len, col_max + widths.m_col_padding, 0);
        }
#if defined(COLOR_SUPPORT)
        if (_rl_colored_stats)
        {
            append_default_color();
            append_color_indicator(C_CLR_TO_EOL);
        }
#endif

        {
            int lines_for_row = 1;
            if (printed_len)
                lines_for_row += (printed_len - 1) / _rl_screenwidth;
            if (_rl_page_completions && lines > 0 && lines + lines_for_row >= _rl_screenheight)
            {
                lines = ___rl_internal_pager(lines);
                if (lines < 0)
                    break;
            }
            lines += lines_for_row;
        }

        flush_tmpbuf();
        rl_crlf();
#if defined(SIGWINCH)
        if (RL_SIG_RECEIVED() && RL_SIGWINCH_RECEIVED() == 0)
#else
        if (RL_SIG_RECEIVED())
#endif
            break;
    }

    return 0;
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
    end_prompt(1/*crlf*/);

    if (_rl_pager_color)
        _rl_print_pager_color();
    fprintf(rl_outstream, "Display all %d possibilities? (y or n)", len);
    if (_rl_pager_color)
        fprintf(rl_outstream, _normal_color);
    fflush(rl_outstream);
    if (__get_y_or_n(0) == 0)
    {
        rl_crlf();
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
extern "C" void display_matches(char** matches)
{
    match_adapter* adapter = nullptr;
    char** rebuilt = nullptr;
    char* rebuilt_storage[3];

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

            adapter = new match_adapter;
            adapter->set_filtered_matches(filtered_matches);
            filtered_matches = nullptr;
        }
    }

    if (!adapter)
    {
        // Handle "simple" case first.  What if there is only one answer?
        if (matches[1] == 0)
        {
            // Rebuild a matches array that has a first match, so the display
            // routine can handle descriptions, and also special display when using
            // match display filtering.
            rebuilt = rebuilt_storage;
            rebuilt[0] = matches[0];
            rebuilt[1] = matches[0];
            rebuilt[2] = 0;
            matches = rebuilt;
            create_matches_lookaside(rebuilt);
        }

        adapter = new match_adapter;
        adapter->set_alt_matches(matches, false);
    }

    int presuf = 0;
    str<32> lcd;
    adapter->get_lcd(lcd);
    if (*__printable_part(const_cast<char*>(lcd.c_str())))
    {
        presuf = bit_prefix|bit_suffix;
        for (int l = adapter->get_match_count(); presuf && l--;)
        {
            if (adapter->is_append_display(l))
                continue;

            const match_type type = adapter->get_match_type(l);
            if (!adapter->use_display(l, type, false))
                continue;

            const char* const match = adapter->get_match(l);
            const char* const display = adapter->get_match_display(l);
            const char* const visible = __printable_part(const_cast<char*>(match));
            const int bits = calc_prefix_or_suffix(visible, display);
            presuf &= bits;
        }
    }

    const int count = adapter->get_match_count();
    const bool best_fit = g_match_best_fit.get();
    const int limit_fit = g_match_limit_fitted.get();
    const bool one_column = adapter->has_descriptions() && count <= DESC_ONE_COLUMN_THRESHOLD;
    const column_widths widths = calculate_columns(adapter, best_fit ? limit_fit : -1, one_column, false, 0, presuf);

    // If there are many items, then ask the user if she really wants to see
    // them all.
    if ((rl_completion_auto_query_items && _rl_screenheight > 0) ?
        display_match_list_internal(adapter, widths, 1, presuf) >= (_rl_screenheight - (_rl_vis_botlin + 1)) :
        rl_completion_query_items > 0 && count >= rl_completion_query_items)
    {
        if (!prompt_display_matches(count))
            goto done;
    }

    display_match_list_internal(adapter, widths, 0, presuf);

done:
    destroy_matches_lookaside(rebuilt);
    delete adapter;
    rl_forced_update_display();
    rl_display_fixed = 1;
}

//------------------------------------------------------------------------------
void override_match_line_state::override(int start, int end, const char* needle, char quote_char)
{
    assert(g_rl_buffer);
    m_line.clear();
    m_line.concat(g_rl_buffer->get_buffer(), start);
    if (quote_char)
        m_line.concat(&quote_char, 1);
    m_line.concat(needle);
    int point = m_line.length();
    m_line.concat(g_rl_buffer->get_buffer() + end);
    override_line_state(m_line.c_str(), needle, point);
}

//------------------------------------------------------------------------------
char need_leading_quote(const char* match)
{
    if (!rl_completion_found_quote &&
        rl_completer_quote_characters &&
        rl_completer_quote_characters[0] &&
        // On Windows, quoting is also needed for non-filename completion, so
        // rl_filename_quoting_desired alone says whether quoting is desired.
        //rl_filename_completion_desired &&
        rl_filename_quoting_desired &&
        rl_filename_quote_characters &&
        _rl_strpbrk(match, rl_filename_quote_characters) != 0)
    {
        return rl_completer_quote_characters[0];
    }
    return 0;
}
