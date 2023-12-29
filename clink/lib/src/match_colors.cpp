/*
    Customizable colors for matches.

    Rewritten and expanded by Christopher Antos for Clink.

    Some portions loosely adapted from parse-colors.c, which is
    Copyright (C) 1985, 1988, 1990-1991, 1995-2010, 2012, 2017
    Free Software Foundation, Inc.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Modified by Chet Ramey for Readline.  */
/* Written by Richard Stallman and David MacKenzie.  */
/* Color support by Peter Anvin <Peter.Anvin@linux.org> and Dennis
   Flaherty <dennisf@denix.elk.miles.com> based on original patches by
   Greg Lee <lee@uhunix.uhcc.hawaii.edu>.  */

#include "pch.h"
#include <assert.h>
#include <core/debugheap.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <wildmatch/wildmatch.h>

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

static const auto& LS_COLORS_indicator = _rl_color_indicator;
#define _rl_color_indicator __do_not_use__

//------------------------------------------------------------------------------
#ifdef INCLUDE_MATCH_COLORING_RULES
static const char c_match_colors_help[] =
"This string is a series of one or more color definition rules separated by ':'\n"
"characters.  The colors are applied when displaying file and directory match\n"
"completions.\n"
"\n"
"Each rule is a series of one or more conditions separated by spaces, followed\n"
"by an equal sign and then the SGR parameters for an ANSI escape code.  All of\n"
"the conditions must be true for the rule to match (in other words, a space is\n"
"like an AND operator).\n"
"\n"
"Each condition can be any of the following:\n"
"    - A pattern, for example \"*.zip\" (for zip files).  This is an fnmatch\n"
"      pattern (like .gitignore globbing patterns).  The pattern is compared\n"
"      only to the filename portion after stripping the path.\n"
"    - A type, for example \"di\" (for directories).  The available types are\n"
"      listed below.\n"
"    - A \"not\" operator, which negates the next condition.  For example,\n"
"      \"not di\" applies to anything that isn't a directory, or \"not *.zip\"\n"
"      applies to any name that doesn't match \"*.zip\".\n"
"\n"
"Any quoted string is assumed to be a pattern, so \"hi\" is a pattern instead\n"
"of the Hidden type, and etc.\n"
"\n"
"Rules are evaluated in the order listed, with one exception:  Rules with\n"
"exactly one type and no patterns are evaluated last; this makes it easier to\n"
"list the rules -- you can put the defaults first, followed by specializations.\n"
"\n"
"The available types are:\n"
"    di          Directory.\n"
"    ex          Executable file.\n"
"    fi          Normal file.\n"
"    ro          Readonly file or directory.\n"
"    hi          Hidden file or directory.\n"
"    mi          Missing file or directory.\n"
"    ln          Symlink; \"ln=target\" uses the color from the target.\n"
"    or          Orphaned symlink (the target of the symlink is missing).\n"
"    no          Normal color; covers anything not covered by any other types.\n"
"    any         Clears all types in the rule, including the implicit default\n"
"                \"fi\" type when no type is given.\n"
"\n"
"For backward compatibility with LS_COLORS, either \"so\" or\n"
"\"*.readline-colored-completion-prefix\" may be used to override the\n"
"color.common_match_prefix setting.\n"
"\n"
"If an environment variable %CLINK_MATCH_COLORS% exists then its value\n"
"supersedes this setting.\n"
"\n"
"NOTE:  This is similar to how the %LS_COLORS% environment variable works,\n"
"except this adds \"hi\", \"ro\", \"any\", and \"not\", and patterns can be\n"
"fnmatch patterns instead of just *.ext patterns.\n"
"\n"
"Examples:\n"
"\n"
"    di=94:di *.tmp=90\n"
"            Directories in bright blue, but dirs ending in .tmp in magenta.\n"
"    di=93:ro ex=1;32:ex=1:ro=32\n"
"            Directories in bright yellow, readonly executable files in bold\n"
"            green, readonly files in green, and executable files in bold."
;

static setting_str g_match_colordef(
    "match.coloring_rules",
    "Coloring rules for completions",
    c_match_colors_help,
    "");
#endif

static setting_color g_common_match_prefix(
    "color.common_match_prefix",
    "Common prefix of completions",
    "Used when displaying prefix that matches have in common.",
    "");

//------------------------------------------------------------------------------
#define CFLAG_NORM                      0x0001
#define CFLAG_FILE                      0x0002
#define CFLAG_DIR                       0x0004
#define CFLAG_READONLY                  0x0008
#define CFLAG_HIDDEN                    0x0010
#define CFLAG_SYMLINK                   0x0020
#define CFLAG_EXEC                      0x0040
#define C_READONLY                      (C_NUM_INDICATORS + 0)
#define C_HIDDEN                        (C_NUM_INDICATORS + 1)
#define C_NUM_INDICATORS_EXTENDED       (C_NUM_INDICATORS + 2)
static const struct {
    const char* name;
    int32 rlind;
    int32 cflag;
} c_indicators[] =
{
    { "no", C_NORM,     CFLAG_NORM },
    { "fi", C_FILE,     CFLAG_FILE },
    { "di", C_DIR,      CFLAG_DIR },
    { "ro", C_READONLY, CFLAG_READONLY },
    { "hi", C_HIDDEN,   CFLAG_HIDDEN },
    { "ln", C_LINK,     CFLAG_SYMLINK },
    { "ex", C_EXEC,     CFLAG_EXEC },
    { "mi", C_MISSING,  0 },
    { "or", C_ORPHAN,   0 },
    { "so", C_SOCK,     0 },
};

//------------------------------------------------------------------------------
static char* s_colors[C_NUM_INDICATORS_EXTENDED] = {};
static const char* s_error_context = nullptr;

//------------------------------------------------------------------------------
bool is_colored(indicator_no colored_filetype)
{
    char const* s = s_colors[colored_filetype];
    if (!s || !*s)          return false;   // Empty.
    else if (*(s++) != '0') return true;
    else if (!*s)           return false;   // "0".
    else if (*(s++) != '0') return true;
    else if (!*s)           return false;   // "00".
    else                    return true;
}

//------------------------------------------------------------------------------
struct color_pattern
{
    str<8> m_pattern;                       // Wildmatch pattern to compare.
    bool m_only_filename;                   // Compare pattern to filename portion only.
    bool m_not;                             // Use the inverse of whether it matches.
};

struct color_rule
{
    int32 m_cflags;                         // Flags that must be set.
    int32 m_not_cflags;                     // Flags that must NOT be set.
    std::vector<color_pattern> m_patterns;  // Wildmatch patterns to match.
    str<16> m_seq;                          // The sequence to output when matched.
};

static std::vector<color_rule> s_color_rules;
static str_moveable s_completion_prefix;
static bool s_norm_colored = false;
static bool s_colored_stats = false;

//------------------------------------------------------------------------------
static char* copy_str(const char* str, int32 len)
{
    char* p = str ? (char*)malloc(len + 1) : nullptr;
    if (p)
    {
        memcpy(p, str, len);
        p[len] = 0;
    }
    return p;
}

//------------------------------------------------------------------------------
// Returns the delimiter character that was encountered, or 0 for NUL
// terminator, or -1 for syntax error.
//
// The following backslash escape sequences are supported:
//      \a      ->  BEL.
//      \b      ->  BACKSPACE.
//      \e      ->  ESCAPE.
//      \f      ->  FORM FEED.
//      \n      ->  NEWLINE.
//      \r      ->  CARRIAGE RETURN.
//      \t      ->  TAB.
//      \v      ->  VTAB.
//      \?      ->  DELETE.
//      \_      ->  SPACE.
//      \000    ->  Octal.
//      \x00    ->  Hex (8-bit, UTF8).
//      \X00    ->  Hex (8-bit, UTF8).
//      \u0000  ->  Hex (16-bit, UTF16, which gets converted to UTF8).
//      \U0000  ->  Hex (16-bit, UTF16, which gets converted to UTF8).
//      \...    ->  Any other character following Backslash is accepted as
//                  itself, including \: \= \" \^ \\ and so on.
//
// There are three ways to prevent spaces from being treated as delimiters:
//      - Surround the string with quotes:      "abc def"
//      - Use the \_ escape sequence:           abc\_def
//      - Use \ followed by a space:            abc\ def
static int32 get_token(str_iter& iter, str_base& token, char delim1, char delim2, bool verbatim, bool* quoted=nullptr)
{
    int32 ret = 0;
    const char* syntax = iter.get_pointer();

    token.clear();
    if (quoted)
        *quoted = false;

    enum { STATE_TEXT, STATE_QUOTE, STATE_BACKSLASH, STATE_OCTAL, STATE_HEX, STATE_CARET, STATE_END, STATE_ERROR } state = STATE_TEXT;
    int32 digits_remaining = 0;
    int32 num = 0;

    while (state < STATE_END)
    {
        const char* const ptr = iter.get_pointer();
        const int32 c = iter.next();

        switch (state)
        {
        case STATE_TEXT:
            switch (c)
            {
            case 0:
            case ':':
                state = STATE_END;
                break;
            case '\"':
                state = STATE_QUOTE;
                syntax = ptr;
                if (quoted)
                    *quoted = true;
                if (verbatim)
                    goto concat_text;
                break;
            case '\\':
                state = STATE_BACKSLASH;
                syntax = ptr;
                if (verbatim)
                    goto concat_text;
                break;
            case '^':
                state = STATE_CARET;
                syntax = ptr;
                if (verbatim)
                    goto concat_text;
                break;
            default:
                if (c == delim1 || c == delim2)
                {
                    ret = c;
                    state = STATE_END;
                    goto done;
                }
concat_text:
                token.concat(ptr, int32(iter.get_pointer() - ptr));
                break;
            }
            break;

        case STATE_QUOTE:
            if (c == 0)
            {
                state = STATE_ERROR;
                _rl_errmsg("%s: missing end quote: %s", s_error_context, syntax);
                break;
            }
            if (c == '\"')
            {
                state = STATE_TEXT;
                if (!verbatim)
                    break;
            }
            goto concat_text;

        case STATE_BACKSLASH:
            switch (c)
            {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                state = STATE_OCTAL;
                digits_remaining = 2;
                num = c - '0';
                if (verbatim)
                    goto concat_text;
                break;
            case 'x':
            case 'X':
                state = STATE_HEX;
                digits_remaining = 2;
                num = 0;
                if (verbatim)
                    goto concat_text;
                break;
            case 'u':
            case 'U':
                state = STATE_HEX;
                digits_remaining = 4;
                num = 0;
                if (verbatim)
                    goto concat_text;
                break;
            case 0:
                state = STATE_ERROR;
                _rl_errmsg("%s: syntax error: %s", s_error_context, syntax);
                break;
            case 'a':   num = '\a'; break;  // Bell
            case 'b':   num = '\b'; break;  // Backspace
            case 'e':   num = 27;   break;  // Escape
            case 'f':   num = '\f'; break;  // Form feed
            case 'n':   num = '\n'; break;  // Newline
            case 'r':   num = '\r'; break;  // Carriage return
            case 't':   num = '\t'; break;  // Tab
            case 'v':   num = '\v'; break;  // VTab
            case '?':   num = 127;  break;  // Delete
            case '_':   num = ' ';  break;  // Space
            default:    num = c;    break;  // ...itself...
            }
            if (STATE_BACKSLASH == state)
            {
concat_numeric:
                state = STATE_TEXT;
                if (verbatim)
                    goto concat_text;
                WCHAR wc = num;
                to_utf8(token, wstr_iter(&wc, 1));
            }
            break;

        case STATE_OCTAL:
            assert(digits_remaining > 0);
            if (c >= '0' && c <= '7')
            {
                num = (num << 3) + (c - '0');
consume_digit:
                if (--digits_remaining <= 0)
                    goto concat_numeric;
                if (verbatim)
                    goto concat_text;
            }
            else
            {
                iter.reset_pointer(ptr);
                goto concat_numeric;
            }
            break;

        case STATE_HEX:
            assert(digits_remaining > 0);
            if (c >= '0' && c <= '9')
            {
                num = (num << 4) + (c - '0');
                goto consume_digit;
            }
            else if (c >= 'a' && c <= 'f')
            {
                num = (num << 4) + 10 + (c - 'a');
                goto consume_digit;
            }
            else if (c >= 'A' && c <= 'F')
            {
                num = (num << 4) + 10 + (c - 'A');
                goto consume_digit;
            }
            else
            {
                iter.reset_pointer(ptr);
                goto concat_numeric;
            }
            break;

        case STATE_CARET:
assert(false);
            if (c >= '@' && c <= '~')
            {
                num = c & 0x1f;
                goto concat_numeric;
            }
            else if (c == '?')
            {
                num = 127;
                goto concat_numeric;
            }
            else
            {
                state = STATE_ERROR;
                _rl_errmsg("%s: syntax error: %s", s_error_context, syntax);
            }
            break;
        }
    }

    if (STATE_ERROR == state)
    {
        _rl_colored_completion_prefix = false;
        _rl_colored_stats = false;
        return -1;
    }

done:
    return ret;
}

//------------------------------------------------------------------------------
static int32 get_token_and_value(str_iter& iter, str_base& token, str_base& value)
{
    int32 r;
    const char* const orig = iter.get_pointer();

    r = get_token(iter, token, ':', '=', true/*verbatim*/);
    if (r < 0)
        return -1;
    if (r != '=')
    {
        _rl_errmsg("%s: missing = sign: %s", s_error_context, orig);
        return -1;
    }

    r = get_token(iter, value, ':', 0, false/*verbatim*/);
    if (r < 0)
        return -1;

    token.trim();
    return !token.empty();
}

//------------------------------------------------------------------------------
static bool parse_rule(str_iter& iter, str<16>& value, color_rule& rule)
{
    uint32 num_flags = 0;
    int32 rlind = -1;
    rule.m_cflags = 0;
    rule.m_not_cflags = 0;

// printf("RULE - parse '%.*s'\n", iter.length(), iter.get_pointer());

    str<> token;
    bool ever_any = false;
    bool not = false;
    while (iter.more())
    {
        bool quoted;
        if (get_token(iter, token, ' ', 0, false/*verbatim*/, &quoted) < 0)
            return false;
        if (!token.length())
            continue;

        if (token.iequals("not"))
        {
            not = true;
            continue;
        }

        bool found = false;
        if (!quoted)
        {
            for (const auto& ind : c_indicators)
            {
                if (token.iequals(ind.name))
                {
                    num_flags++;
                    rlind = ind.rlind;
                    if (not)
                        rule.m_not_cflags |= ind.cflag;
                    else
                        rule.m_cflags |= ind.cflag;
                    found = true;
                    break;
                }
            }

            if (token.iequals("any"))
            {
                rule.m_cflags = 0;
                rule.m_not_cflags = 0;
                num_flags = 0;
                ever_any = true;
                found = true;
            }
        }

        if (!found)
        {
            color_pattern pat;
            pat.m_pattern.concat(token.c_str(), token.length());
            //pat.m_only_filename = !strpbrk(token.c_str(), "/\\");
            pat.m_only_filename = true;
            pat.m_not = not;
// printf("pat '%s'%s\n", pat.m_pattern.c_str(), not ? " (not)" : "");
            rule.m_patterns.emplace_back(std::move(pat));
        }

        not = false;
    }

    // Readonly by itself should set CFLAG_READONLY|CFLAG_FILE so by itself it
    // only applies to files, but can be explicitly applied to dirs/links/etc.
    if (rule.m_cflags == CFLAG_READONLY && rule.m_patterns.empty() && num_flags == 1)
        rule.m_cflags |= CFLAG_FILE;

    // Patterns with no attributes should set CFLAG_FILE so they only apply to
    // files, but can be explicitly applied to dirs/links/etc.
    if (rule.m_patterns.size() && !rule.m_cflags && !rule.m_not_cflags && !ever_any)
        rule.m_cflags |= CFLAG_FILE;

// printf("cflags 0x%04.4x\n", rule.m_cflags);
// printf("not_cflags 0x%04.4x\n", rule.m_not_cflags);

    if (rule.m_cflags && num_flags == 1 && !rule.m_not_cflags && rule.m_patterns.empty())
    {
        assert(rlind >= 0);
        free(s_colors[rlind]);
        s_colors[rlind] = copy_str(value.c_str(), value.length());
        assert(s_colors[rlind]);
        if (rlind == C_SOCK)
            s_completion_prefix = value.c_str();
        return false;
    }

    if (rule.m_cflags || rule.m_not_cflags || rule.m_patterns.size())
    {
        rule.m_seq = std::move(value);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
// Syntax:
//
// The string is a series of one or more rules, delimited by : characters.
//
// Each rule is a series of one or more conditions, delimited by spaces.  All
// of the conditions must be true for the rule to match (in other words, a
// space is like an AND operator).
//
// Conditions can be the same as in the LS_COLORS environment variable.
// In addition, the following are also supported:
//
//      hi          Hidden flag.
//      ro          Readonly flag.
//      any         Clears all flags in the rule, including implicit flags.
//      not         Negate the next condition ("not di" means when not a
//                  directory, or "not *.txt" means when *.txt pattern doesn't
//                  match.
//      pattern     Anything else is an fnmatch pattern.  But if the pattern
//                  doesn't contain any path separators, then the pattern is
//                  compared only to the filename portion of any match.
//
// Any quoted string is assumed to be a pattern, so you can use "hi" as a
// pattern instead of the Hidden flag, and etc.
//
// Rules are evaluated in the order listed, with one exception:  Rules with
// exactly one flag and no patterns are evaluated last; this makes it easier
// to list the rules -- you can put the defaults first, followed by
// specializations.
//
// If an environment variable %CLINK_MATCH_COLORS% exists then its value
// supersedes this setting.
//
// Example:
//
//      di=94:di *.tmp=90
//              Directories in bright blue, but dirs ending in .tmp in dark
//              gray (bright black).
void parse_match_colors()
{
    assert(!s_error_context);

    dbg_ignore_scope(snapshot, "parse match colors");

    std::vector<color_rule> empty;
    s_color_rules.swap(empty);
    g_common_match_prefix.get(s_completion_prefix);
    s_colored_stats = false;

    // Parse LS_COLORS only if completion colors are enabled.
    if (_rl_colored_stats || _rl_colored_completion_prefix)
    {
        _rl_parse_colors();

        if (_rl_colored_completion_prefix > 0)
        {
            // If C_SOCK was overridden, then that supersedes the
            // g_common_match_prefix setting.  But override it early so that
            // *.readline-colored-completion-prefix can supersede C_SOCK.
            const auto sock = LS_COLORS_indicator[C_SOCK];
            if (sock.string != c_default_completion_prefix_color)
            {
                s_completion_prefix.clear();
                s_completion_prefix.concat(sock.string, sock.len);
            }
        }

        str<> ext;
        for (const COLOR_EXT_TYPE* e = _rl_color_ext_list; e; e = e->next)
        {
            ext.clear();
            ext.concat(e->ext.string, e->ext.len);
            if (_rl_colored_completion_prefix &&
                ext.equals(".readline-colored-completion-prefix"))
            {
                s_completion_prefix.clear();
                s_completion_prefix.concat(e->seq.string, e->seq.len);
                break;
            }
        }
    }

    // Always copy the LS_COLORS indicators, because clink-select-complete
    // needs C_LEFT/etc to be initialized.
    for (int32 i = 0; i < sizeof_array(s_colors); ++i)
    {
        free(s_colors[i]);

        const char* str;
        int32 len;
        if (i < C_NUM_INDICATORS)
        {
            const struct bin_str* s;
            s = &LS_COLORS_indicator[i];
            str = s->string;
            len = s->len;
        }
        else
        {
            if (i == C_READONLY)
                str = _rl_readonly_color;
            else if (i == C_HIDDEN)
                str = _rl_hidden_color;
            else
                str = nullptr;
            len = str ? int32(strlen(str)) : 0;
        }

        s_colors[i] = copy_str(str, len);
    }

    // Parse match coloring rules, if any.
    {
        str<> s;
        s_error_context = "CLINK_MATCH_COLORS";
        if (!os::get_env("CLINK_MATCH_COLORS", s))
        {
#ifdef INCLUDE_MATCH_COLORING_RULES
            s_error_context = g_match_colordef.get_name();
            g_match_colordef.get(s);
#endif
        }

        if (s.length())
        {
            str<> token;
            str<16> value;
            str_iter iter(s.c_str(), s.length());
            str<16> readline_colored_completion_prefix;
            bool override = false;
            while (iter.more())
            {
                const int32 r = get_token_and_value(iter, token, value);
                if (r < 0)
                {
                    _rl_errmsg("unparsable value for %s string", s_error_context);
                    break;
                }
                if (r > 0)
                {
                    if (token.equals("*.readline-colored-completion-prefix"))
                    {
                        s_colored_stats = true;
                        override = true;
                        readline_colored_completion_prefix = value.c_str();
                    }
                    else
                    {
                        color_rule rule;
                        if (parse_rule(str_iter(token.c_str(), token.length()), value, rule))
                        {
                            s_colored_stats = true;
                            s_color_rules.emplace_back(std::move(rule));
                        }
                    }
                }
            }

            if (override)
                s_completion_prefix = readline_colored_completion_prefix.c_str();
        }
    }

    s_norm_colored = is_colored(C_NORM);

    assert(s_colors[C_LEFT]);
    assert(s_colors[C_RIGHT]);
    assert(s_colors[C_RESET]);
    assert(s_colors[C_LINK]);

    s_error_context = nullptr;
}

//------------------------------------------------------------------------------
bool using_match_colors()
{
    return _rl_colored_stats || s_colored_stats;
}

//------------------------------------------------------------------------------
static void ls_concat_color_indicator(enum indicator_no colored_filetype, str_base& out)
{
    const struct bin_str *ind = &LS_COLORS_indicator[colored_filetype];
    out.concat(ind->string, ind->len);
}

//------------------------------------------------------------------------------
void ls_make_color(const char* seq, int32 len, str_base& out)
{
    // Need to reset so not dealing with attribute combinations.
    if (s_norm_colored)
    {
        ls_concat_color_indicator(C_LEFT, out);
        ls_concat_color_indicator(C_RIGHT, out);
    }
    ls_concat_color_indicator(C_LEFT, out);
    out.concat(seq, len);
    ls_concat_color_indicator(C_RIGHT, out);
}

//------------------------------------------------------------------------------
void make_color(const char* seq, str_base& out)
{
    // Need to reset so not dealing with attribute combinations.
    if (s_norm_colored)
        out << s_colors[C_LEFT] << s_colors[C_RIGHT];
    out << s_colors[C_LEFT] << seq << s_colors[C_RIGHT];
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
            if (linkok && _strnicmp(LS_COLORS_indicator[C_LINK].string, "target", 6) == 0)
                mode = linkstat.st_mode;
        }
        else
#endif
            linkok = 1;
    }
    else
        linkok = -1;

    // Is this a nonexistent file?  If so, linkok == -1.

    if (linkok == -1 && LS_COLORS_indicator[C_MISSING].string != nullptr)
        colored_filetype = C_MISSING;
#if defined(S_ISLNK)
    else if (linkok == 0 && S_ISLNK(mode) && LS_COLORS_indicator[C_ORPHAN].string != nullptr)
        colored_filetype = C_ORPHAN; // dangling symlink.
#endif
    else if (stat_ok != 0)
        colored_filetype = C_FILE;
    else
    {
        if (S_ISREG(mode))
            colored_filetype = ((mode & S_IXUGO) != 0 && is_colored(C_EXEC)) ? C_EXEC : C_FILE;
        else if (S_ISDIR(mode))
            colored_filetype = C_DIR;
#if defined(S_ISLNK)
        else if (S_ISLNK(mode) && _strnicmp(LS_COLORS_indicator[C_LINK].string, "target", 6) != 0)
            colored_filetype = C_LINK;
#endif
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
            ls_make_color(override_color, int32(strlen(override_color)), out);
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
        const struct bin_str* const s = ext ? &(ext->seq) : &LS_COLORS_indicator[colored_filetype];
        if (s->string != nullptr)
        {
            ls_make_color(s->string, s->len, out);
            return true;
        }
        else
            return false;
    }
}

//------------------------------------------------------------------------------
bool get_match_color(const char* f, match_type type, str_base& out)
{
    if (!using_match_colors())
    {
        out.clear();
        return false;
    }

    if (is_match_type(type, match_type::cmd))
    {
        make_color(_rl_command_color, out);
        return true;
    }
    else if (is_match_type(type, match_type::alias))
    {
        make_color(_rl_alias_color, out);
        return true;
    }

    // Fall back to LS_COLORS behaviors when needed.
    if (s_color_rules.empty())
        return get_ls_color(f, type, out);

    // This should already have undergone tilde expansion.
    const char* name = f;
    char* filename = 0;
    if (rl_filename_stat_hook)
    {
        filename = savestring(f);
        (*rl_filename_stat_hook)(&filename);
        name = filename;
    }

    // Get stat info.
    struct stat astat, linkstat;
    int32 stat_ok;
    if (!is_zero(type))
    {
#if defined(HAVE_LSTAT)
        stat_ok = stat_from_match_type(static_cast<match_type_intrinsic>(type), name, &astat, &linkstat);
#else
        stat_ok = stat_from_match_type(static_cast<match_type_intrinsic>(type), name, &astat);
#endif
    }
    else
    {
#if defined(HAVE_LSTAT)
        stat_ok = lstat(name, &astat);
#else
        stat_ok = stat(name, &astat);
#endif
    }

    mode_t mode;
    int32 linkok; // 1 == ok, 0 == dangling symlink, -1 == missing.
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
            if (linkok && _strnicmp(s_colors[C_LINK], "target", 6) == 0)
                mode = linkstat.st_mode;
        }
        else
#endif
            linkok = 1;
    }
    else
    {
        // Is this a nonexistent file?  If so, linkok == -1.
        linkok = -1;
    }

    // Identify flags for matching, and identify default color type.
    int32 cflags;
    indicator_no colored_filetype;
    if (linkok == -1 && s_colors[C_MISSING] != nullptr)
    {
        colored_filetype = C_MISSING;
        cflags = 0;
    }
#if defined(S_ISLNK)
    else if (linkok == 0 && S_ISLNK(mode) && s_colors[C_ORPHAN] != nullptr)
    {
        colored_filetype = C_ORPHAN; // dangling symlink.
        cflags = 0;
    }
#endif
    else
    {
        if (stat_ok != 0)
        {
            colored_filetype = C_FILE;
            cflags = CFLAG_FILE;
        }
        else if (S_ISREG(mode))
        {
            colored_filetype = C_FILE;
            cflags = CFLAG_FILE;
            if ((mode & S_IXUGO) != 0)
            {
                colored_filetype = C_EXEC;
                cflags |= CFLAG_EXEC;
            }
        }
        else if (S_ISDIR(mode))
        {
            colored_filetype = C_DIR;
            cflags = CFLAG_DIR;
        }
#if defined(S_ISLNK)
        else if (S_ISLNK(mode) && _strnicmp(s_colors[C_LINK], "target", 6) != 0)
        {
            colored_filetype = C_LINK;
            cflags = CFLAG_SYMLINK;
        }
#endif
        else
        {
            colored_filetype = C_ORPHAN;
            cflags = 0;
        }

        if (!is_zero(type))
        {
            if (!is_pathish(type))
            {
                colored_filetype = C_NORM;
                cflags = 0;
            }
            else if (cflags)
            {
                if (is_match_type_readonly(type))
                {
                    colored_filetype = indicator_no(C_READONLY);
                    cflags |= CFLAG_READONLY;
                }
                // Hidden takes precedence over Readonly, if no rules match.
                if (is_match_type_hidden(type))
                {
                    colored_filetype = indicator_no(C_HIDDEN);
                    cflags |= CFLAG_HIDDEN;
                }
            }
        }
    }

    str<280> tmp;
    if (cflags & CFLAG_DIR)
    {
        tmp = name;
        while (true)
        {
            const int32 len = tmp.length();
            if (!len)
                break;
            if (!path::is_separator(tmp.c_str()[len - 1]))
                break;
            tmp.truncate(len - 1);
        }
        name = tmp.c_str();
        // Directory takes precedence over Readonly, if no rules match.
        if (colored_filetype == indicator_no(C_READONLY))
            colored_filetype = indicator_no(C_DIR);
    }

    // Look for a matching rule.  First match wins.
    const char* seq = nullptr;
    const int32 bits = WM_CASEFOLD|WM_SLASHFOLD|WM_WILDSTAR;
    str<> no_trailing_sep;
    str<> only_name;
    for (const auto& rule : s_color_rules)
    {
        // Try to match flags.
        if (rule.m_cflags && (cflags & rule.m_cflags) != rule.m_cflags)
            goto next_rule;
        if (rule.m_not_cflags && (cflags & rule.m_not_cflags) != 0)
            goto next_rule;

        // Try to match patterns.
        for (const auto& pat : rule.m_patterns)
        {
            const char* n = name;
            if (cflags & CFLAG_DIR)
            {
                if (no_trailing_sep.empty())
                {
                    no_trailing_sep = name;
                    path::maybe_strip_last_separator(no_trailing_sep);
                }
                n = no_trailing_sep.c_str();
            }
            if (pat.m_only_filename)
            {
                if (only_name.empty())
                    only_name = path::get_name(n);
                n = only_name.c_str();
            }

            if (wildmatch(pat.m_pattern.c_str(), n, bits) != (pat.m_not ? WM_NOMATCH : WM_MATCH))
                goto next_rule;
        }

        // Match found!
        seq = rule.m_seq.c_str();
        break;

next_rule:
        continue;
    }

    if (!seq)
        seq = s_colors[colored_filetype];

    free(filename); // nullptr or savestring return value.

    if (seq)
    {
        make_color(seq, out);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
const char* get_indicator_color(indicator_no colored_filetype)
{
    return s_colors[colored_filetype];
}

//------------------------------------------------------------------------------
const char* get_completion_prefix_color()
{
    if (!s_completion_prefix.empty())
        return s_completion_prefix.c_str();

    if (_rl_colored_completion_prefix > 0)
        return get_indicator_color(C_PREFIX);

    return nullptr;
}

//------------------------------------------------------------------------------
extern "C" void _rl_print_pager_color()
{
    str<16> s;
    make_color(_rl_pager_color, s);
    fwrite(s.c_str(), s.length(), 1, rl_outstream);
}
