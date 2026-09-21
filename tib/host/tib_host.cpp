// Copyright (c) 2026 by Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_host.h"

#include <locale.h>

namespace tib_host {

#ifdef _WIN32
void set_crt_locale_utf8()
{
    setlocale(LC_ALL, ".utf8");
}

void set_console_vt_input()
{
    DWORD mode;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_INPUT);
}
#endif

} // namespace tib_host
