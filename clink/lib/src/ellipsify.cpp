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
            const char* prev = code.get_pointer();
            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (const int32 c = inner_iter.next())
            {
                const int32 clen = (expand_ctrl && (CTRL_CHAR(c) || c == RUBOUT)) ? 2 : clink_wcwidth(c);
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
                out.concat(prev, int32(inner_iter.get_pointer() - prev));
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
