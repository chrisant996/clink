// Copyright (c) 2012 Martin Ridgers
// Portions Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "usage.h"
#include "version.h"

#include <core/base.h>
#include <core/str.h>
// #include <core/str_iter.h>
#include <core/settings.h>
#include <terminal/ecma48_wrapper.h>

//------------------------------------------------------------------------------
static constexpr const char* const c_clink_header =
    "Clink v" CLINK_VERSION_STR "\n"
    ORIGINAL_COPYRIGHT_STR "\n"
    PORTIONS_COPYRIGHT_STR "\n"
    "https://github.com/chrisant996/clink\n"
    ;

static constexpr const char* const c_clink_header_abbr =
    "Clink v" CLINK_VERSION_STR " (https://github.com/chrisant996/clink)\n"
    ;

static setting_enum s_clink_logo(
    "clink.logo",
    "Controls what startup logo to show",
    "The default is 'full' which shows the full copyright logo when Clink is\n"
    "injected.  A value of 'short' shows an abbreviated startup logo with version\n"
    "information.  A value of 'none' omits the startup logo entirely.",
    "none,full,short",
    1);

//------------------------------------------------------------------------------
void maybe_print_logo()
{
    const int32 logo = s_clink_logo.get();
    if (!logo)
        return;

    // Add a blank line if our logo follows anything else (the goal is to
    // put a blank line after CMD's "Microsoft Windows ..." logo), but don't
    // add a blank line if our logo is at the very top of the window.
    CONSOLE_SCREEN_BUFFER_INFO csbi = { sizeof(csbi) };
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        if (csbi.dwCursorPosition.Y > 0)
            puts("");
    }

    // Using printf instead of puts ensures there's only one blank line
    // between the header and the subsequent prompt.
    printf("%s", (logo == 2) ? c_clink_header_abbr : c_clink_header);
}

//------------------------------------------------------------------------------
void puts_clink_header()
{
    puts(c_clink_header);
}

//------------------------------------------------------------------------------
static void print_with_wrapping(int32 max_len, const char* arg, const char* desc, uint32 wrap)
{
    str<128> buf;
    ecma48_wrapper wrapper(desc, wrap);
    while (wrapper.next(buf))
    {
        printf("  %-*s  %s", max_len, arg, buf.c_str());
        arg = "";
    }
}

//------------------------------------------------------------------------------
void puts_help(const char* const* help_pairs, const char* const* other_pairs)
{
    int32 max_len = -1;
    for (int32 i = 0; help_pairs[i]; i += 2)
        max_len = max((int32)strlen(help_pairs[i]), max_len);
    if (other_pairs)
        for (int32 i = 0; other_pairs[i]; i += 2)
            max_len = max((int32)strlen(other_pairs[i]), max_len);

    DWORD wrap = 78;
#if 0
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        if (csbi.dwSize.X >= 40)
            csbi.dwSize.X -= 2;
        wrap = max<DWORD>(wrap, min<DWORD>(100, csbi.dwSize.X));
    }
#endif
    if (wrap <= DWORD(max_len + 20))
        wrap = 0;
    if (wrap)
        wrap -= 2 + max_len + 2;

    for (int32 i = 0; help_pairs[i]; i += 2)
    {
        const char* arg = help_pairs[i];
        const char* desc = help_pairs[i + 1];
        print_with_wrapping(max_len, arg, desc, wrap);
    }

    puts("");
}
