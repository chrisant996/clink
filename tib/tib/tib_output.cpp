// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_terminal.h"
#include "tib_termcap.h"
#include "wcwidth.h"
#include <assert.h>

namespace tib {

class basic_terminal_out final : public terminal_out
{
public:
                        basic_terminal_out() noexcept;
    void                write(const char* s, size_t len) noexcept override;
    void                ding() noexcept override;

private:
#ifdef _WIN32
    HANDLE              m_hout = 0;
    bool                m_is_console = false;
    // Conversion buffer.  Must be a member (not global) otherwise atexit
    // cleanup functions may destroy the buffer out of dependency order.
    cstring_t<WCHAR>    m_tmp;
#endif
};

terminal_out* new_basic_terminal_out()
{
    return new basic_terminal_out;
}

basic_terminal_out::basic_terminal_out() noexcept
{
    DWORD mode;
    m_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    m_is_console = !!GetConsoleMode(m_hout, &mode);
}

void basic_terminal_out::write(const char* s, size_t len) noexcept
{
#ifdef _WIN32
    DWORD written;
    if (m_is_console)
    {
        if (!to_utf16(s, len, m_tmp))
            return;
        WriteConsoleW(m_hout, m_tmp.c_str(), DWORD(m_tmp.length()), &written, nullptr);
    }
    else
    {
        WriteFile(m_hout, s, DWORD(len), &written, nullptr);
    }
#else
    fwrite(s, len, 1, stdout);
#endif
}

void basic_terminal_out::ding() noexcept
{
    write("\007", 1);
}

size_t fits_in_wcwidth(const char* s, const size_t len, const uint16_t truncate_width, uint16_t* truncated_width)
{
    uint16_t length_fits = 0;
    uint16_t width_fits = 0;
    uint16_t width = 0;

    wcwidth_iter iter(s, len);
    while (true)
    {
        const char32_t c = iter.next();
        if (!c)
            break;

        const uint16_t w = iter.character_wcwidth_onectrl();
        width += w;

        if (width > truncate_width)
        {
            if (truncated_width)
                *truncated_width = width_fits;
            return length_fits;
        }

        length_fits = unsigned(iter.get_pointer() - s);
        width_fits = width;
    }

    if (truncated_width)
        *truncated_width = width_fits;
    return length_fits;
}

} // namespace tib
