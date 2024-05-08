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
#include "match_colors.h"
#include "matches_lookaside.h"
#include "matches_impl.h"
#include "match_adapter.h"
#include "column_widths.h"
#include "ellipsify.h"
#include "line_buffer.h"
#include "pager.h"

#include <core/base.h>
#include <core/path.h>
#include <core/os.h>
#include <core/settings.h>
#include <terminal/ecma48_iter.h>
#include <terminal/wcwidth.h>

extern "C" {

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

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
int __get_y_or_n (int for_pager);
char* __printable_part (char* pathname);
int __stat_char (const char *filename, char match_type);

} // extern "C"

#define ELLIPSIS_LEN ellipsis_len



//------------------------------------------------------------------------------
extern setting_bool g_match_best_fit;
extern setting_int g_match_limit_fitted;
extern line_buffer* g_rl_buffer;
int _rl_display_matches_prompted = false;
rl_match_display_filter_func_t *rl_match_display_filter_func = nullptr;
const char *_rl_description_color = nullptr;
const char *_rl_filtered_color = nullptr;
const char *_rl_arginfo_color = nullptr;
const char *_rl_selected_color = nullptr;



//------------------------------------------------------------------------------
static char* tmpbuf_allocated = nullptr;
static char* tmpbuf_ptr = nullptr;
static int32 tmpbuf_length = 0;
static int32 tmpbuf_capacity = 0;
static int32 tmpbuf_rollback_length = 0;
static const char* const _normal_color = "\x1b[m";
static const int32 _normal_color_len = 3;

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
static void grow_tmpbuf (int32 growby)
{
    int32 needsize = tmpbuf_length + growby + 1;
    if (needsize <= tmpbuf_capacity)
        return;

    int32 oldsize = tmpbuf_capacity;
    int32 newsize;
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
void append_tmpbuf_string(const char* s, int32 len)
{
    if (len < 0)
        len = strlen(s);

    grow_tmpbuf(len);

    memcpy(tmpbuf_ptr, s, len);
    tmpbuf_ptr += len;
    tmpbuf_length += len;
}

//------------------------------------------------------------------------------
void append_tmpbuf_string_colorless(const char* s, int32 len)
{
    str<> tmp;
    ecma48_processor(s, &tmp, nullptr, ecma48_processor_flags::colorless);
    append_tmpbuf_string(tmp.c_str(), tmp.length());
}

//------------------------------------------------------------------------------
const char* get_tmpbuf_rollback (void)
{
    grow_tmpbuf(1);
    *tmpbuf_ptr = '\0';
    return tmpbuf_allocated + tmpbuf_rollback_length;
}

//------------------------------------------------------------------------------
uint32 calc_tmpbuf_cell_count(void)
{
    uint32 count = 0;

    ecma48_state state;
    ecma48_iter iter(tmpbuf_allocated, state, tmpbuf_length);
    while (const ecma48_code& code = iter.next())
    {
        if (code.get_type() != ecma48_code::type_chars)
            continue;

        count += clink_wcswidth(code.get_pointer(), code.get_length());
    }

    return count;
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
    const char* s = get_indicator_color(colored_filetype);
    if (s)
        append_tmpbuf_string(s, -1);
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
    const char* s = get_completion_prefix_color();
    if (s != nullptr)
    {
        if (is_colored(C_NORM))
            append_default_color();
        append_color_indicator(C_LEFT);
        append_tmpbuf_string(s, -1);
        append_color_indicator(C_RIGHT);
    }
}

static void append_match_color_indicator(const char *f, match_type type)
{
    str<32> seq;
    get_match_color(f, type, seq);
    append_tmpbuf_string(seq.c_str(), seq.length());
}

static void prep_non_filename_text(void)
{
    if (get_indicator_color(C_END) != nullptr)
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
static int32
path_isdir(match_type type, const char *filename)
{
    if (!is_zero(type) && !is_match_type(type, match_type::none))
        return is_match_type(type, match_type::dir);

    struct stat finfo;
    return (stat(filename, &finfo) == 0 && S_ISDIR(finfo.st_mode));
}



//------------------------------------------------------------------------------
static int32 fnappend(const char *to_print, int32 prefix_bytes, int32 condense, const char *real_pathname, match_type match_type, int32 selected)
{
    int32 printed_len = 0;
    int32 common_prefix_len = 0;

    // Don't print only the ellipsis if the common prefix is one of the
    // possible completions.  Only cut off prefix_bytes if we're going to be
    // printing the ellipsis, which takes precedence over coloring the
    // completion prefix (see append_filename() below).
    if (condense && prefix_bytes >= strlen(to_print))
        prefix_bytes = 0;

    if (selected)
        append_selection_color();
#if defined(COLOR_SUPPORT)
    else if (using_match_colors() && (prefix_bytes == 0 || !get_completion_prefix_color()))
        append_colored_stat_start(real_pathname, match_type);
#endif

    if (prefix_bytes && condense)
    {
        char ellipsis = (to_print[prefix_bytes] == '.') ? '_' : '.';
#if defined(COLOR_SUPPORT)
        if (!selected)
            append_colored_prefix_start();
#endif
        for (int32 i = ELLIPSIS_LEN; i--;)
            append_tmpbuf_char(ellipsis);
#if defined(COLOR_SUPPORT)
        if (!selected)
        {
            append_colored_prefix_end();
            if (using_match_colors())
                append_colored_stat_start(real_pathname, match_type);
        }
#endif
        printed_len = ELLIPSIS_LEN;
    }
#if defined(COLOR_SUPPORT)
    else if (prefix_bytes && get_completion_prefix_color())
    {
        common_prefix_len = prefix_bytes;
        prefix_bytes = 0;
        // Print color indicator start here.
        if (!selected)
            append_colored_prefix_start();
    }
#endif

    wcwidth_iter iter(to_print + prefix_bytes);
    while (const uint32 c = iter.next())
    {
        if (CTRL_CHAR(c))
        {
            append_tmpbuf_char('^');
            append_tmpbuf_char(UNCTRL(c));
            printed_len += 2;
        }
        else if (c == RUBOUT || iter.character_wcwidth_signed() < 0)
        {
            append_tmpbuf_string("^?", 2);
            printed_len += 2;
        }
        else
        {
            append_tmpbuf_string(iter.character_pointer(), iter.character_length());
            printed_len += iter.character_wcwidth_onectrl();
        }
        if (common_prefix_len > 0 && (iter.get_pointer() - to_print) >= common_prefix_len)
        {
#if defined(COLOR_SUPPORT)
            // printed bytes = s - to_print
            // printed bytes should never be > but check for paranoia's sake
            if (!selected)
            {
                append_colored_prefix_end();
                if (using_match_colors())
                    append_colored_stat_start(real_pathname, match_type);
            }
#endif
            common_prefix_len = 0;
        }
    }

#if defined (COLOR_SUPPORT)
    if (using_match_colors() && !selected)
        append_colored_stat_end();
#endif

    return printed_len;
}

//------------------------------------------------------------------------------
static void handle_leading_display_space(const char*& to_print, int32 selected)
{
    if (*to_print == ' ')
    {
        if (selected)
            append_tmpbuf_string("\x1b[23;24;29m", 11);
        else
            append_tmpbuf_string(_normal_color, _normal_color_len);

        while (*to_print == ' ')
        {
            append_tmpbuf_string(to_print, 1);
            to_print++;
        }
    }
}

//------------------------------------------------------------------------------
void append_display(const char* to_print, int32 selected, const char* color)
{
    if (selected)
    {
        append_selection_color();
        if (color)
        {
            str<16> tmp;
            ecma48_processor(color, &tmp, nullptr, ecma48_processor_flags::colorless);
            handle_leading_display_space(to_print, selected);
            append_tmpbuf_string(tmp.c_str(), tmp.length());
        }
    }
    else
    {
        append_default_color();
        if (color)
        {
            handle_leading_display_space(to_print, selected);
            append_tmpbuf_string(color, -1);
        }
    }

    append_tmpbuf_string(to_print, -1);

    if (selected)
        append_tmpbuf_string("\x1b[23;24;29m", 11);
    else
        append_default_color();
}

//------------------------------------------------------------------------------
// Print filename.  If VISIBLE_STATS is defined and we are using it, check for
// and output a single character for 'special' filenames.  Return the number of
// characters we output.
// The optional VIS_STAT_CHAR receives the visual stat char.  This is to allow
// the visual stat char to show up correctly even after an ellipsis.
int32 append_filename(char* to_print, const char* full_pathname, int32 prefix_bytes, int32 condense, match_type type, int32 selected, int32* vis_stat_char)
{
    int32 printed_len, extension_char, slen, tlen;
    char *s, c, *new_full_pathname;
    const char *dn;
    char tmp_slash[3];

    int32 filename_display_desired = rl_filename_display_desired || is_match_type(type, match_type::dir);

    extension_char = 0;
#if defined(COLOR_SUPPORT)
    // Defer printing if we want to prefix with a color indicator.
    if (!using_match_colors() || filename_display_desired == 0)
#endif
        printed_len = fnappend(to_print, prefix_bytes, condense, to_print, type, selected);

    if (filename_display_desired && (
#if defined (VISIBLE_STATS)
        rl_visible_stats ||
#endif
#if defined (COLOR_SUPPORT)
        using_match_colors() ||
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
            if (using_match_colors())
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
            if (using_match_colors())
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
            if (using_match_colors() && extension_char == rl_preferred_path_separator)
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
            if (using_match_colors() && !selected && extension_char == rl_preferred_path_separator)
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
void pad_filename(int32 len, int32 pad_to_width, int32 selected)
{
    const bool exact = selected || pad_to_width < 0;
    int32 num_spaces = 0;
    if (pad_to_width < 0)
        pad_to_width = -pad_to_width;
    if (pad_to_width <= len)
        num_spaces = exact ? 0 : 1;
    else
        num_spaces = pad_to_width - len;

#if defined(COLOR_SUPPORT)
    if (using_match_colors() && selected == 0)
        append_default_color();
#endif

    while (num_spaces > 0)
    {
        static const char spaces[] = "                                                ";
        const int32 spaces_bytes = sizeof(spaces) - sizeof(spaces[0]);
        const int32 chunk_len = (num_spaces < spaces_bytes) ? num_spaces : spaces_bytes;
        append_tmpbuf_string(spaces, chunk_len);
        num_spaces -= spaces_bytes;
    }

    if (selected > 0)
        append_default_color();
}

//------------------------------------------------------------------------------
int32 __fnwidth(const char* string)
{
    int32 width = 0;

    wcwidth_iter iter(string);
    while (iter.next())
        width += iter.character_wcwidth_twoctrl();

    return width;
}



//------------------------------------------------------------------------------
inline bool exact_match(const char* match, const char* text, uint32 len)
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
static int32 calc_prefix_or_suffix(const char* match, const char* display)
{
    // Sample scenarios, and whether they're handled:
    //  1.  YES:  "match"
    //  2.  YES:  "symbol match"
    //  3.  YES:  "match symbol"
    //  4.  NO:   "symbol match symbol"
    //  5.  NO:   "{color}m{color}a{color}t{color}c{color}h"
    //  6.  YES:  "{color}symbol{color}match"
    //  7.  YES:  "{color}match{color}symbol"
    //  8.  YES:  "{color}symbol{color}match{color}symbol"
    //
    // The intent is to find a segment of text long enough to contain the
    // match text, and do processing on it.  If a segment isn't long enough to
    // contain the match text, then there's no way to reliably predict which
    // portions of the text are additional markup versus actual match text.

    const uint32 match_len = uint32(strlen(match));

    bool pre = false;
    bool suf = false;
    int32 visible_len = 0;

    ecma48_state state;
    ecma48_iter iter(display, state);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            assert(code.get_length());
            suf = false;

            // Ignore substrings between ECMA48 codes that aren't long enough
            // to contain the match text.
            if (match_len <= code.get_length())
            {
                // Only in the first substring long enough to contain the
                // match text, test if match is a prefix of the substring
                // (cases 1, 3, 6, 7, 8).
                if (!visible_len)
                    pre = exact_match(match, code.get_pointer(), code.get_length());
                // In all substrings long enough to contain the match text,
                // test if match is a suffix of the substring (case 2).
                suf = exact_match(match, code.get_pointer() + code.get_length() - match_len, match_len);
            }

            visible_len += clink_wcswidth(code.get_pointer(), code.get_length());
        }

    return (pre ? bit_prefix : 0) + (suf ? bit_suffix : 0);
}

//------------------------------------------------------------------------------
int32 append_tmpbuf_with_visible_len(const char* s, int32 len)
{
    append_tmpbuf_string(s, len);

    int32 visible_len = 0;

    ecma48_state state;
    ecma48_iter iter(s, state, len);
    while (const ecma48_code& code = iter.next())
        if (code.get_type() == ecma48_code::type_chars)
        {
            visible_len += clink_wcswidth(code.get_pointer(), code.get_length());
        }

    return visible_len;
}

//------------------------------------------------------------------------------
static int32 append_prefix_complex(const char* text, int32 len, const char* leading, int32 prefix_bytes, int32 condense)
{
    int32 visible_len = 0;

    // Prefix.
    append_colored_prefix_start();
    if (condense && prefix_bytes < len)
    {
        const char ellipsis = (text[prefix_bytes] == '.') ? '_' : '.';
        for (int32 i = ELLIPSIS_LEN; i--;)
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
uint32 append_display_with_presuf(const char* match, const char* display, int32 presuf, int32 prefix_bytes, int32 condense, match_type type)
{
    match = __printable_part((char*)match);

    assert(presuf);
    bool pre = !!(presuf & bit_prefix);
    bool suf = !pre && (presuf & bit_suffix);
    const int32 skip_cells = suf ? cell_count(display) - cell_count(match) : 0;

    int32 visible_len = 0;
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
            wcwidth_iter inner_iter(code.get_pointer(), code.get_length());
            while (true)
            {
                if (suf && visible_len == skip_cells)
                {
                    const uint32 rest_len = inner_iter.length();
                    if (prefix_bytes <= rest_len)
                    {
                        suf = false;
                        did = true;
                        // Already counted in visible_len.
                        append_tmpbuf_string(code.get_pointer(), uint32(inner_iter.get_pointer() - code.get_pointer()));
                        visible_len += append_prefix_complex(inner_iter.get_pointer(), rest_len, leading.c_str(), prefix_bytes, condense);
                        break;
                    }
                }

                if (!inner_iter.next())
                    break;
                visible_len += inner_iter.character_wcwidth_onectrl();
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
void print_pager_prompt(bool help)
{
    if (_rl_pager_color)
        _rl_print_pager_color();
    fprintf(rl_outstream, "-- More %s--", help ? "[<space><tab><enter>dynq] " : "");
    if (_rl_pager_color)
        fprintf(rl_outstream, "\x1b[m");
}

//------------------------------------------------------------------------------
static int internal_pager(int lines)
{
    bool help = false;

again:
    print_pager_prompt(help);

    const int i = __get_y_or_n(1);
    _rl_erase_entire_line();

    switch (i)
    {
    case 1: return 0;           // One line.
    case 2: return lines - 1;   // One page.
    case 3: return lines / 2;   // Half page.
    case 4: help = true; _rl_erase_entire_line(); goto again;
    }

    return -1;
}



//------------------------------------------------------------------------------
static int32 display_match_list_internal(const match_adapter& adapter, const column_widths& widths, bool only_measure, int32 presuf)
{
    const int32 count = adapter.get_match_count();
    int32 printed_len;
    const char* description_color = "\x1b[m";
    int32 description_color_len = 3;

    const int32 cols = __complete_get_screenwidth();
    const int32 show_descriptions = adapter.has_descriptions();
    const int32 limit = widths.num_columns();
    assert(limit > 0);

    // How many iterations of the printing loop?
    const int32 rows = (count + (limit - 1)) / limit;

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
    const int32 major_stride = _rl_print_completions_horizontally ? limit : 1;
    const int32 minor_stride = _rl_print_completions_horizontally ? 1 : rows;

    if (_rl_description_color)
    {
        description_color = _rl_description_color;
        description_color_len = strlen(description_color);
    }

    int32 lines = 0;
    for (int32 i = 0; i < rows; i++)
    {
        reset_tmpbuf();
        for (int32 j = 0, l = i * major_stride; j < limit; j++)
        {
            if (l >= count)
                break;

            const int32 col_max = ((show_descriptions && !widths.m_right_justify) ?
                                   cols - 1 :
                                   widths.column_width(j)); // Allow to wrap lines.

            const match_type type = adapter.get_match_type(l);
            const char* const match = adapter.get_match(l);
            const char* const display = adapter.get_match_display(l);
            const bool append = adapter.is_append_display(l);

            if (adapter.use_display(l, type, append))
            {
                printed_len = 0;
                if (append)
                {
                    char* temp = __printable_part((char*)match);
                    printed_len = append_filename(temp, match, widths.m_sind, widths.m_can_condense, type, 0, nullptr);
                    append_display(display, 0, _rl_arginfo_color);
                    printed_len += adapter.get_match_visible_display(l);
                }
                else if (presuf)
                {
                    printed_len += append_display_with_presuf(match, display, presuf, widths.m_sind, widths.m_can_condense, type);
                }
                else
                {
                    append_display(display, 0, _rl_filtered_color);
                    printed_len += adapter.get_match_visible_display(l);
                }
            }
            else
            {
                char* temp = __printable_part((char*)display);
                printed_len = append_filename(temp, display, widths.m_sind, widths.m_can_condense, type, 0, nullptr);
            }

            if (show_descriptions)
            {
                const char* const description = adapter.get_match_description(l);
                if (description && *description)
                {
                    const bool right_justify = widths.m_right_justify;
#ifdef USE_DESC_PARENS
                    const int32 parens = right_justify ? 2 : 0;
#else
                    const int32 parens = 0;
#endif
                    const int32 pad_to = (right_justify ?
                        max<int32>(printed_len + widths.m_desc_padding, col_max - (adapter.get_match_visible_description(l) + parens)) :
                        widths.max_match_len(j) + widths.m_desc_padding);
                    if (pad_to < cols - 1)
                    {
                        pad_filename(printed_len, pad_to, 0);
                        printed_len = pad_to + parens;
                        append_tmpbuf_string(description_color, description_color_len);
                        if (parens)
                        {
                            append_tmpbuf_string("(", 1);
                            mark_tmpbuf();
                        }
                        printed_len += ellipsify_to_callback(description, col_max - printed_len, false/*expand_ctrl*/, append_tmpbuf_string);
                        if (parens)
                        {
                            if (strchr(get_tmpbuf_rollback(), '\x1b'))
                                append_tmpbuf_string(description_color, description_color_len);
                            append_tmpbuf_string(")", 1);
                        }
                        append_tmpbuf_string(_normal_color, _normal_color_len);
                    }
                }
            }

            l += minor_stride;

            if (j + 1 < limit && l < count)
                pad_filename(printed_len, col_max + widths.m_col_padding, 0);
        }
#if defined(COLOR_SUPPORT)
        if (using_match_colors())
        {
            append_default_color();
            append_color_indicator(C_CLR_TO_EOL);
        }
#endif

        {
            int32 lines_for_row = 1;
            if (printed_len)
                lines_for_row += (printed_len - 1) / _rl_screenwidth;
            if (_rl_page_completions && lines > 0 && lines + lines_for_row >= _rl_screenheight)
            {
                lines = internal_pager(lines);
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
static int32 prompt_display_matches(int32 len)
{
    end_prompt(1/*crlf*/);

    _rl_display_matches_prompted = true;

    if (_rl_pager_color)
        _rl_print_pager_color();
    fprintf(rl_outstream, "Display all %d possibilities? (y or n)", len);
    if (_rl_pager_color)
        fwrite(_normal_color, strlen(_normal_color), 1, rl_outstream);
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
    match_adapter adapter;
    char** rebuilt = nullptr;
    char* rebuilt_storage[3];

    _rl_display_matches_prompted = false;

    // If there is a display filter, give it a chance to modify MATCHES.
    if (rl_match_display_filter_func)
    {
        matches_impl* filtered_matches = new matches_impl;
        if (rl_match_display_filter_func(matches, *filtered_matches))
        {
            if (!filtered_matches->get_match_count())
            {
                rl_ding();
                delete filtered_matches;
                return;
            }

            adapter.set_filtered_matches(filtered_matches, true/*own*/);

            // Readline calls rl_ignore_some_completions_function before
            // calling display_matches.  Must reapply match filtering after
            // applying match display filtering.
            adapter.filter_matches();

            filtered_matches = nullptr; // Ownership was transferred.
        }
        delete filtered_matches;
    }

    if (!adapter.is_initialized())
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

        adapter.set_alt_matches(matches, false);
    }

    int32 presuf = 0;
    str<32> lcd;
    adapter.get_lcd(lcd);
    if (*__printable_part(const_cast<char*>(lcd.c_str())))
    {
        presuf = bit_prefix|bit_suffix;
        for (int32 l = adapter.get_match_count(); presuf && l--;)
        {
            if (adapter.is_append_display(l))
                continue;

            const match_type type = adapter.get_match_type(l);
            if (!adapter.use_display(l, type, false))
                continue;

            const char* const match = adapter.get_match(l);
            const char* const display = adapter.get_match_display(l);
            const char* const visible = __printable_part(const_cast<char*>(match));
            const int32 bits = calc_prefix_or_suffix(visible, display);
            presuf &= bits;
        }
    }

    const int32 count = adapter.get_match_count();
    const bool best_fit = g_match_best_fit.get();
    const int32 limit_fit = g_match_limit_fitted.get();
    const bool one_column = adapter.has_descriptions() && count <= DESC_ONE_COLUMN_THRESHOLD;
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
    rl_forced_update_display();
    rl_display_fixed = 1;
}

//------------------------------------------------------------------------------
void rl_display_match_list(char **matches, int len, int max)
{
    // WARNING:  This does not use matches lookaside; it's exclusively so
    // _rl_display_cmdname_matches works for completing command names when
    // "execute-named-command" uses the readstr functions.

    match_adapter adapter;

    if (!adapter.is_initialized())
        adapter.set_alt_matches(matches, false);

    str<32> lcd;
    adapter.get_lcd(lcd);

    const int32 count = adapter.get_match_count();
    const bool best_fit = g_match_best_fit.get();
    const int32 limit_fit = g_match_limit_fitted.get();
    const column_widths widths = calculate_columns(adapter, best_fit ? limit_fit : -1, false, false, 0, 0);

    display_match_list_internal(adapter, widths, 0, 0);
}

//------------------------------------------------------------------------------
void override_match_line_state::override(int32 start, int32 end, const char* needle)
{
    override(start, end, needle, need_leading_quote(needle));
}

//------------------------------------------------------------------------------
void override_match_line_state::override(int32 start, int32 end, const char* needle, char quote_char)
{
    assert(g_rl_buffer);
    m_line.clear();
    m_line.concat(g_rl_buffer->get_buffer(), start);
    if (quote_char)
        m_line.concat(&quote_char, 1);
    m_line.concat(needle);
    int32 point = m_line.length();
    m_line.concat(g_rl_buffer->get_buffer() + end);
    override_line_state(m_line.c_str(), needle, point);
}

//------------------------------------------------------------------------------
void override_match_line_state::fully_qualify(int32 start, int32 end, str_base& needle)
{
    str<280> tmp;

    if (path::get_drive(needle.c_str(), tmp))
        tmp.clear();
    else
    {
        os::get_current_dir(tmp);
        if (!path::get_drive(tmp))
            return;
    }

    if (!tmp.concat(needle.c_str()))
        return;
    if (tmp.empty())
        return;

    str_moveable dir;
    str_moveable name;
    if (!path::get_directory(tmp.c_str(), dir) || !path::get_name(tmp.c_str(), name))
        return;

    path::normalise(dir);
    os::get_full_path_name(dir.c_str(), tmp);
    path::append(tmp, name.c_str());

    needle.clear();
    needle.concat(tmp.c_str(), tmp.length());
    override(start, end, needle.c_str());
}

//------------------------------------------------------------------------------
char need_leading_quote(const char* match)
{
    if (!rl_completion_found_quote &&
        rl_completer_quote_characters &&
        rl_completer_quote_characters[0] &&
        rl_need_match_quoting(match))
    {
        return rl_completer_quote_characters[0];
    }
    return 0;
}
