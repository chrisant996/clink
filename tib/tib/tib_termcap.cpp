// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#ifdef _MSC_VER
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   ifndef _CRT_NONSTDC_NO_WARNINGS
#       define _CRT_NONSTDC_NO_WARNINGS
#   endif
#endif

#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_terminal.h"
#include "tib_termcap.h"

namespace tib {

#ifdef _WIN32

const char c_hide_cursor[] = "\x1b[?25l";
const char c_show_cursor[] = "\x1b[?25h";

bool ensure_term_caps()
{
    return true;
}

void uninit_term_caps()
{
}

const char* term_row_col(int32_t row, int32_t col)
{
    static cstring s_buffer;
    s_buffer.clear();
    s_buffer.printf("\x1b[%d;%dH", row, col);
    return s_buffer.c_str();
}

const char* term_col(int32_t col)
{
    if (col > 1)
    {
        static cstring s_buffer;
        s_buffer.clear();
        s_buffer.printf("\x1b[%dG", col);
        return s_buffer.c_str();
    }
    return "\r";
}

const char* term_erase_to_eol()
{
    return "\x1b[K";
}

const char* term_move_up(int32_t num_rows)
{
    static cstring s_buffer;
    s_buffer.clear();
    s_buffer.printf("\x1b[%dA", num_rows);
    return s_buffer.c_str();
}

const char* term_move_down(int32_t num_rows)
{
    static cstring s_buffer;
    s_buffer.clear();
    s_buffer.printf("\x1b[%dB", num_rows);
    return s_buffer.c_str();
}

coord get_terminal_size()
{
    coord size = { 80, 25 };
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hout, &csbi))
    {
        size.x = csbi.dwSize.X;
        size.y = csbi.dwSize.Y;
    }
    return size;
}

#else

// TODO-LINUX: cross platform terminal support.

// "am" -- Automatic margins; printing in the last column automatically wraps to the next line.
// "xn" -- The cursor wraps in a strange way (refer to termcap documentation for details).
// "LP" -- Safe to print in the last column without worrying about undesired scrolling (the DEC flavor of 'xn').
// "km" -- The terminal has a Meta key (e.g. ALT).

coord get_terminal_size()
{
    coord size;
    size.x = tgetnum("co");
    size.y = tgetnum("li");
    return size;
}

#endif

}
