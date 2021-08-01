// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str.h"
#include "str_transform.h"

#include <assert.h>

//------------------------------------------------------------------------------
#if defined(__MINGW32__) || defined(__MINGW64__)
inline int __ascii_towlower(int c)
{
    if (c >= 0 && c <= 127)
        return tolower(c);
    return c;
}

inline int __ascii_towupper(int c)
{
    if (c >= 0 && c <= 127)
        return toupper(c);
    return c;
}
#endif

//------------------------------------------------------------------------------
void str_transform(const wchar_t* in, unsigned int len, wstr_base& out, transform_mode mode)
{
    DWORD mapflags;

    switch (mode)
    {
    case transform_mode::lower:     mapflags = LCMAP_LOWERCASE; break;
    case transform_mode::upper:     mapflags = LCMAP_UPPERCASE; break;
    case transform_mode::title:     mapflags = LCMAP_TITLECASE; break;
    default:                        assert(false); return;
    }

    out.reserve(len + max<unsigned int>(len / 10, 10));
    int cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, len, out.data(), out.size());
    if (!cch)
    {
        cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, len, nullptr, 0);
        out.reserve(cch + 1);
        cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, len, out.data(), out.size());
        if (!cch)
        {
            out.clear();
            bool title_char = true;
            for (unsigned int i = 0; i < len; ++i)
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
