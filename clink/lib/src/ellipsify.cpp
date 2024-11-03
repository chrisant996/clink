// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ellipsify.h"

#include <core/str.h>
#include <core/path.h>
#include <terminal/ecma48_iter.h>
#include <terminal/wcwidth.h>

#include <vector>

extern "C" {
#include <readline/readline.h>
#include <readline/chardefs.h>
};

//------------------------------------------------------------------------------
#ifdef USE_ASCII_ELLIPSIS
const char* const ellipsis = "...";
static_assert(ellipsis_len == 3, "Ellipsis length is inaccurate.");
static_assert(ellipsis_cells == 3, "Ellipsis column width is inaccurate.");
#else
const char* const ellipsis = "\xe2\x80\xa6";
static_assert(ellipsis_len == 3, "Ellipsis length is inaccurate.");
static_assert(ellipsis_cells == 1, "Ellipsis column width is inaccurate.");
#endif

//------------------------------------------------------------------------------
int32 ellipsify_to_callback(const char* in, int32 limit, int32 expand_ctrl, vstrlen_func_t callback)
{
    str<> s;
    int32 visible_len = ellipsify(in, limit, s, !!expand_ctrl);
    callback(s.c_str(), s.length());
    return visible_len;
}

//------------------------------------------------------------------------------
// Parse ANSI escape codes to determine the visible character length of the
// string (which gets used for column alignment).  Truncate the string with an
// ellipsis if it exceeds a maximum visible length.
//
// Returns the visible character length of the output string.
//
// Pass true for expand_ctrl if control characters will end up being displayed
// as two characters, e.g. "^C" or "^[".
int32 ellipsify(const char* in, int32 limit, str_base& out, bool expand_ctrl)
{
    int32 visible_len = 0;
    int32 truncate_visible = -1;
    int32 truncate_bytes = -1;

    out.clear();

    ecma48_state state;
    ecma48_iter iter(in, state);
    while (visible_len <= limit)
    {
        const ecma48_code& code = iter.next();
        if (!code)
            break;
        if (code.get_type() == ecma48_code::type_chars)
        {
            wcwidth_iter inner_iter(code.get_pointer(), code.get_length());
            while (const int32 c = inner_iter.next())
            {
                const int32 clen = (inner_iter.character_wcwidth_signed() < 0) ? (expand_ctrl ? 2 : 1) : inner_iter.character_wcwidth_signed();
                assert(clen >= 0);
                if (truncate_visible < 0 && visible_len + clen > limit - ellipsis_cells)
                {
                    truncate_visible = visible_len;
                    truncate_bytes = out.length();
                }
                if (visible_len + clen > limit)
                {
                    out.truncate(truncate_bytes);
                    visible_len = truncate_visible;
#ifdef USE_ASCII_ELLIPSIS
                    out.concat(ellipsis, min<int32>(ellipsis_len, max<int32>(0, limit - truncate_visible)));
#else
                    static_assert(ellipsis_cells == 1, "Ellipsis must be exactly 1 cell.");
                    assert(cell_count(ellipsis) == 1);
                    out.concat(ellipsis, ellipsis_len);
#endif
                    visible_len += cell_count(ellipsis);
                    return visible_len;
                }
                visible_len += clen;
                out.concat(inner_iter.character_pointer(), inner_iter.character_length());
            }
        }
        else
        {
            out.concat(code.get_pointer(), code.get_length());
        }
    }

    return visible_len;
}

//------------------------------------------------------------------------------
// Parse ANSI escape codes to determine the visible character length of the
// string (which gets used for column alignment).  Truncate the string with an
// ellipsis if it exceeds a maximum visible length.
//
// Returns the visible character length of the output string.
//
// Pass true for expand_ctrl if control characters will end up being displayed
// as two characters, e.g. "^C" or "^[".
int32 ellipsify_ex(const char* in, int32 limit, ellipsify_mode mode, str_base& out, const char* const _ellipsis, bool expand_ctrl, bool* truncated)
{
    int32 visible_len = 0;
    int32 truncate_visible = -1;
    int32 truncate_bytes = -1;

    out.clear();
    if (truncated)
        *truncated = false;

    // Does the whole string fit?
    const int32 total_cells = cell_count(in);
    if (total_cells <= limit)
    {
no_truncate:
        out = in;
        return total_cells;
    }

    const int32 e_len = _ellipsis ? str_len(_ellipsis) : ellipsis_len;
    const int32 e_cells = _ellipsis ? cell_count(_ellipsis) : ellipsis_cells;
    const char* const e = _ellipsis ? _ellipsis : ellipsis;

    ecma48_state state;
    ecma48_iter iter(in, state);
    switch (mode)
    {
    default:
    case RIGHT:
        while (visible_len <= limit)
        {
            const ecma48_code& code = iter.next();
            if (!code)
                break;
            if (code.get_type() == ecma48_code::type_chars)
            {
                wcwidth_iter inner_iter(code.get_pointer(), code.get_length());
                while (const int32 c = inner_iter.next())
                {
                    const int32 clen = (inner_iter.character_wcwidth_signed() < 0) ? (expand_ctrl ? 2 : 1) : inner_iter.character_wcwidth_signed();
                    assert(clen >= 0);
                    if (truncate_visible < 0 && visible_len + clen > limit - e_cells)
                    {
                        truncate_visible = visible_len;
                        truncate_bytes = out.length();
                    }
                    if (visible_len + clen > limit)
                    {
                        assert(truncate_visible >= 0);
                        out.truncate(truncate_bytes);
                        visible_len = truncate_visible;
                        // Append as much of the ellipsis string as fits (e.g. the
                        // limit could be smaller than the ellipsis string in the
                        // first place).
                        if (limit - visible_len > 0)
                        {
                            str<16> e_out;
                            const int32 e_width = ellipsify_ex(e, limit - visible_len, RIGHT, e_out, "", expand_ctrl);
                            out.concat(e_out.c_str(), e_out.length());
                            visible_len += e_width;
                        }
                        if (truncated)
                            *truncated = true;
                        assert(cell_count(out.c_str()) == visible_len);
                        return visible_len;
                    }
                    visible_len += clen;
                    out.concat(inner_iter.character_pointer(), inner_iter.character_length());
                }
            }
            else
            {
                out.concat(code.get_pointer(), code.get_length());
            }
        }
        return visible_len;

    case LEFT:
        {
            struct run
            {
                int32   index;
                int32   length;
                int32   cells;
                bool    chars;
            };
            std::vector<run> runs;

            // Build vector of runs of individual renderable characters.
            const char* prev_end = in;
            while (const ecma48_code& code = iter.next())
            {
                if (code.get_type() == ecma48_code::type_chars)
                {
                    wcwidth_iter inner_iter(code.get_pointer(), code.get_length());
                    while (const int32 c = inner_iter.next())
                    {
                        run r;
                        r.index = int32(inner_iter.character_pointer() - in);
                        r.length = inner_iter.character_length();
                        r.cells = (inner_iter.character_wcwidth_signed() < 0) ? (expand_ctrl ? 2 : 1) : inner_iter.character_wcwidth_signed();
                        r.chars = true;
                        runs.emplace_back(r);
                    }
                }
                else
                {
                    run r;
                    r.index = int32(prev_end - in);
                    r.length = code.get_length();
                    r.cells = 0;
                    r.chars = false;
                    runs.emplace_back(r);
                }
                prev_end = iter.get_pointer();
            }

            if (runs.empty())
            {
                // Should be impossible, since the first step was to check
                // whether the whole string fits, and an empty string always
                // fits.
                assert(false);
                goto no_truncate;
            }

            // Iterate from right to left over the vector of runs.
            const run* const begin = &runs[0];
            const run* const end = begin + runs.size();
            const run* r = end;
            const run* truncate_run = end;
            while (r-- > begin && visible_len <= limit)
            {
                if (r->chars)
                {
                    const int32 clen = r->cells;
                    assert(clen >= 0);
                    if (truncate_visible < 0 && visible_len + clen > limit - e_cells)
                    {
                        truncate_visible = visible_len;
                        truncate_run = (r + 1);
                        truncate_bytes = (truncate_run < end) ? truncate_run->index : -1;
                    }
                    if (visible_len + clen > limit)
                    {
                        assert(truncate_visible >= 0);

                        // Build the output string.  Start with any leading
                        // escape codes, to maintain consistent styling even
                        // when truncated.
                        for (const run* walk = begin; walk < truncate_run; ++walk)
                        {
                            if (!walk->chars)
                                out.concat(in + walk->index, walk->length);
                        }

                        // Append as much of the ellipsis string as fits (e.g. the
                        // limit could be smaller than the ellipsis string in the
                        // first place).
                        visible_len = truncate_visible;
                        if (limit - visible_len > 0)
                        {
                            str<16> e_out;
                            const int32 e_width = ellipsify_ex(e, limit - visible_len, RIGHT, e_out, "", expand_ctrl);
                            out.concat(e_out.c_str(), e_out.length());
                            visible_len += e_width;
                        }

                        // Append the kept portion of the truncated string.
                        if (truncate_bytes >= 0)
                            out.concat(in + truncate_bytes);
                        if (truncated)
                            *truncated = true;
                        assert(cell_count(out.c_str()) == visible_len);
                        return visible_len;
                    }
                    visible_len += clen;
                }
            }

            // This means the whole string fits.  Which should be impossible
            // to reach, since the first step was to check whether the whole
            // string fits.
            assert(false);
            goto no_truncate;
        }

    case PATH:
        {
            const char* const _in = in;
            str<16> drive;

            // Try to keep the whole drive.  This can't use path::get_drive()
            // because this needs to accommodate ANSI escape codes embedded in
            // the input string.
            const char* drive_end = in;
            int32 drive_chars = 0;
            while (true)
            {
                const ecma48_code& code = iter.next();
                if (!code)
                {
break_break_no:
                    drive_end = nullptr;
break_break_yes:
                    break;
                }
                if (code.get_type() == ecma48_code::type_chars)
                {
                    str_iter inner_iter(code.get_pointer(), code.get_length());
                    while (const int32 c = inner_iter.next())
                    {
                        switch (drive_chars)
                        {
                        case 0:
                            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                                goto break_break_no;
                            ++drive_chars;
                            ++drive_end;
                            break;
                        case 1:
                            if (c != ':')
                                goto break_break_no;
                            ++drive_chars;
                            ++drive_end;
                            break;
                        case 2:
                            goto break_break_yes;
                        }
                    }
                }
                else
                {
                    drive_end = iter.get_pointer();
                }
            }
            if (drive_end)
            {
                assert(2 == drive_chars);
                drive.concat(in, int32(drive_end - in));
                in = drive_end;
            }

            // Try to keep as much of the rest of the path as can fit.
            const int32 drive_cells = cell_count(drive.c_str());
            if (limit >= drive_cells)
            {
                str_moveable rest;
                const int32 rest_cells = ellipsify_ex(in, limit - drive_cells, LEFT, rest, _ellipsis, false, truncated);
                if (rest_cells >= e_cells && limit >= drive_cells + rest_cells)
                {
                    out.concat(drive.c_str(), drive.length());
                    out.concat(rest.c_str(), rest.length());
                    return drive_cells + rest_cells;
                }
            }

            // Couldn't get something to fit?  Fall back to RIGHT truncation.
            return ellipsify_ex(_in, limit, RIGHT, out, _ellipsis, false, truncated);
        }
    }
}
