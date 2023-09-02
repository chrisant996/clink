/*

    Customizable colors for matches.

*/

#include "pch.h"
#include <assert.h>
#include <core/debugheap.h>

#include "match_colors.h"

extern "C" {
#define READLINE_LIBRARY
#define BUILD_READLINE
#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif
#include <readline/rldefs.h>
#include <readline/posixstat.h>
#include <readline/readline.h>
#include "readline/xmalloc.h"
#include <readline/rlprivate.h>
#include <readline/colors.h>
#if defined (COLOR_SUPPORT)
#define PARSE_COLOR_ONLY_FUNCTION_PROTOTYPES
#include <readline/parse-colors.h>
#endif
}

extern bool get_ls_color(const char *f, match_type type, str_base& out);

//------------------------------------------------------------------------------
static bool is_colored(int32 colored_filetype)
{
    const char* s = _rl_color_indicator[colored_filetype].string;
    const char* end = s + _rl_color_indicator[colored_filetype].len;
    if (!s || s >= end)     return false;   // Empty.
    else if (*(s++) != '0') return true;
    else if (s >= end)      return false;   // "0".
    else if (*(s++) != '0') return true;
    else if (s >= end)      return false;   // "00".
    else                    return true;
}

//------------------------------------------------------------------------------
static bool s_norm_colored = false;

//------------------------------------------------------------------------------
void parse_match_colors()
{
    if (!_rl_colored_stats && !_rl_colored_completion_prefix)
        return;

    dbg_ignore_scope(snapshot, "parse match colors");

    _rl_parse_colors();

    s_norm_colored = is_colored(C_NORM);
}

//------------------------------------------------------------------------------
static void concat_color_indicator(enum indicator_no colored_filetype, str_base& out)
{
    const struct bin_str *ind = &_rl_color_indicator[colored_filetype];
    out.concat(ind->string, ind->len);
}

//------------------------------------------------------------------------------
void make_color(const char* seq, int32 len, str_base& out)
{
    // Need to reset so not dealing with attribute combinations.
    if (s_norm_colored)
    {
        concat_color_indicator(C_LEFT, out);
        concat_color_indicator(C_RIGHT, out);
    }
    concat_color_indicator(C_LEFT, out);
    out.concat(seq, len);
    concat_color_indicator(C_RIGHT, out);
}

//------------------------------------------------------------------------------
static bool get_ls_color(const char *f, match_type type, str_base& out)
{
    enum indicator_no colored_filetype;
    COLOR_EXT_TYPE *ext; // Color extension.
    size_t len;          // Length of name.

    const char *name;
    char *filename;
    struct stat astat, linkstat;
    mode_t mode;
    int32 linkok; // 1 == ok, 0 == dangling symlink, -1 == missing.
    int32 stat_ok;

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
        const char *override_color = nullptr;
        if (is_pathish(type))
        {
            // Hidden can override dir and other classifications, but readonly
            // can only override the C_FILE classification.
            if (_rl_hidden_color && is_match_type_hidden(type))
                override_color = _rl_hidden_color;
            else if (colored_filetype != C_FILE)
                override_color = nullptr;
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
            make_color(override_color, int32(strlen(override_color)), out);
            return true;
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
            make_color(s->string, s->len, out);
            return true;
        }
        else
            return false;
    }
}

//------------------------------------------------------------------------------
bool get_match_color(const char* f, match_type type, str_base& out)
{
    if (!_rl_colored_stats)
    {
        out.clear();
        return false;
    }

    return get_ls_color(f, type, out);
}

//------------------------------------------------------------------------------
extern "C" void _rl_print_pager_color()
{
    str<16> s;
    make_color(_rl_pager_color, int32(strlen(_rl_pager_color)), s);
    fwrite(s.c_str(), s.length(), 1, rl_outstream);
}
