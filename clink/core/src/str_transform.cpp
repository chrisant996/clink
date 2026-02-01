// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str.h"
#include "str_iter.h"
#include "str_transform.h"

#include <assert.h>

//------------------------------------------------------------------------------
#if defined(__MINGW32__) || defined(__MINGW64__)
inline int32 __ascii_towlower(int32 c)
{
    if (c >= 0 && c <= 127)
        return tolower(c);
    return c;
}

inline int32 __ascii_towupper(int32 c)
{
    if (c >= 0 && c <= 127)
        return toupper(c);
    return c;
}
#endif

//------------------------------------------------------------------------------
void str_transform(const char* in, uint32 len, str_base& out, transform_mode mode)
{
    wstr<> win;
    wstr<> wout;
    to_utf16(win, in, len);
    str_transform(win.c_str(), win.length(), wout, mode);
    out = wout.c_str();
}

//------------------------------------------------------------------------------
void str_transform(const wchar_t* in, uint32 len, wstr_base& out, transform_mode mode)
{
    DWORD mapflags;

    switch (mode)
    {
    case transform_mode::lower:     mapflags = LCMAP_LOWERCASE; break;
    case transform_mode::upper:     mapflags = LCMAP_UPPERCASE; break;
    case transform_mode::title:     mapflags = LCMAP_TITLECASE; break;
    default:                        assert(false); return;
    }

    if (len == uint32(-1))
        len = uint32(wcslen(in));

    out.reserve(len + max<uint32>(len / 10, 10));
    int32 cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, len, out.data(), out.size());
    if (!cch)
    {
        cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, len, nullptr, 0);
        out.reserve(cch + 1);
        cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, len, out.data(), out.size());
        if (!cch)
        {
            out.clear();
            bool title_char = true;
            for (uint32 i = 0; i < len; ++i)
            {
                WCHAR c = in[i];

                switch (mode)
                {
                case transform_mode::lower:
                    out.data()[i] = __ascii_towlower(c);
                    break;
                case transform_mode::upper:
                    out.data()[i] = __ascii_towupper(c);
                    break;
                case transform_mode::title:
                    out.data()[i] = title_char ? __ascii_towupper(c) : __ascii_towlower(c);
                    break;
                }

                title_char = !!iswspace(c);
            }

            cch = len;
        }
    }

    out.data()[cch] = '\0';
}
