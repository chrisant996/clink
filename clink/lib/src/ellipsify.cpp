// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ellipsify.h"

#include <core/str.h>
#include <terminal/ecma48_iter.h>

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
int ellipsify_to_callback(const char* in, int limit, int expand_ctrl, vstrlen_func_t callback)
{
    str<> s;
    int visible_len = ellipsify(in, limit, s, !!expand_ctrl);
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
int ellipsify(const char* in, int limit, str_base& out, bool expand_ctrl)
{
    int visible_len = 0;
    int truncate_visible = -1;
    int truncate_bytes = -1;

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
            const char* prev = code.get_pointer();
            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (const int c = inner_iter.next())
            {
                const int clen = (expand_ctrl && (CTRL_CHAR(c) || c == RUBOUT)) ? 2 : clink_wcwidth(c);
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
                    out.concat(ellipsis, min<int>(ellipsis_len, max<int>(0, limit - truncate_visible)));
#else
                    static_assert(ellipsis_cells == 1, "Ellipsis must be exactly 1 cell.");
                    assert(cell_count(ellipsis) == 1);
                    out.concat(ellipsis, ellipsis_len);
#endif
                    visible_len += cell_count(ellipsis);
                    return visible_len;
                }
                visible_len += clen;
                out.concat(prev, static_cast<int>(inner_iter.get_pointer() - prev));
                prev = inner_iter.get_pointer();
            }
        }
        else
        {
            out.concat(code.get_pointer(), code.get_length());
        }
    }

    return visible_len;
}
