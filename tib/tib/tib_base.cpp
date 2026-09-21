// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "str_iter.h"
#include <assert.h>

#ifndef _WIN32
#include <time.h>
#endif

namespace tib {

size_t resolve_auto_length(size_t len, const char* s) noexcept
{
    return !s ? 0 : (len == c_auto_length) ? strlen(s) : len;
}

#ifdef _WIN32
size_t resolve_auto_length(size_t len, const WCHAR* s) noexcept
{
    return !s ? 0 : (len == c_auto_length) ? wcslen(s) : len;
}
#endif

int __vsnprintf(char* buffer, size_t len, const char* format, va_list args)
{
    return _vsnprintf_s(buffer, len, _TRUNCATE, format, args);
}

#ifdef _WIN32
int __vsnprintf(WCHAR* const buffer, size_t const len, const WCHAR* const format, va_list args)
{
    return _vsnwprintf_s(buffer, len, _TRUNCATE, format, args);
}
#endif

template<>
const char* const cstring_t<char>::c_spaces = "                                ";

#ifdef _WIN32
template<>
const WCHAR* const cstring_t<WCHAR>::c_spaces = L"                                ";
#endif

template<>
const char* cstring_t<char>::c_str() const noexcept
{
    return m_text ? m_text : "";
}

#ifdef _WIN32
template<>
const WCHAR* cstring_t<WCHAR>::c_str() const noexcept
{
    return m_text ? m_text : L"";
}
#endif

#ifdef _WIN32
bool to_utf8(const WCHAR* s, size_t len, cstring_t<char>& out)
{
    out.clear();

    len = resolve_auto_length(len, s);
    if (len)
    {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, s, int(len), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            return false;
        if (!out.reserve(needed))
            return false;

        const int converted = WideCharToMultiByte(CP_UTF8, 0, s, int(len), out.reserve(0), int(out.capacity()), nullptr, nullptr);
        if (converted <= 0)
        {
            out.clear();
            return false;
        }

        out.set_length(converted);
    }

    return true;
}

bool to_utf16(const char* s, size_t len, cstring_t<WCHAR>& out)
{
    out.clear();

    len = resolve_auto_length(len, s);
    if (len)
    {
        const int needed = MultiByteToWideChar(CP_UTF8, 0, s, int(len), nullptr, 0);
        if (needed <= 0)
            return false;
        if (!out.reserve(needed))
            return false;

        const int converted = MultiByteToWideChar(CP_UTF8, 0, s, int(len), out.reserve(0), int(out.capacity()));
        if (converted <= 0)
        {
            out.clear();
            return false;
        }

        out.set_length(converted);
    }

    return true;
}

static bool map_utf16_case(const WCHAR* in, size_t len, cstring_t<WCHAR>& out, DWORD mapflags)
{
    out.clear();
    if (!len)
        return true;

    if (!out.reserve(len + max<size_t>(len / 10, 10)))
        return false;

    int32_t cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, DWORD(len), out.reserve(0), DWORD(out.capacity()));
    if (!cch)
    {
        cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, DWORD(len), nullptr, 0);
        if (cch <= 0 || !out.reserve(cch))
            return false;
        cch = LCMapStringW(LOCALE_USER_DEFAULT, mapflags, in, DWORD(len), out.reserve(0), DWORD(out.capacity()));
        if (!cch)
            return false;
    }

    out.set_length(cch);
    return true;
}
#endif

bool str_transform(const char* in, size_t len, cstring& out, transform_mode mode)
{
    if (!in)
        return false;

#ifdef _WIN32
    DWORD mapflags;
    switch (mode)
    {
    case transform_mode::lower:     mapflags = LCMAP_LOWERCASE; break;
    case transform_mode::upper:     mapflags = LCMAP_UPPERCASE; break;
    case transform_mode::title:     mapflags = LCMAP_TITLECASE; break;
    default:                        assert(false); return false;
    }
#endif

    len = resolve_auto_length(len, in);
    assert(len <= strlen(in));

#ifdef _WIN32
    cstring_t<WCHAR> tmp_in;
    cstring_t<WCHAR> tmp_out;
    if (!to_utf16(in, len, tmp_in))
        return false;

    if (mode == transform_mode::title)
    {
        cstring_t<WCHAR> tmp_lower;
        if (!map_utf16_case(tmp_in.c_str(), tmp_in.length(), tmp_lower, LCMAP_LOWERCASE))
            return false;
        if (!map_utf16_case(tmp_lower.c_str(), tmp_lower.length(), tmp_out, mapflags))
            return false;
    }
    else
    {
        if (!map_utf16_case(tmp_in.c_str(), tmp_in.length(), tmp_out, mapflags))
            return false;
    }

    return to_utf8(tmp_out.c_str(), tmp_out.length(), out);
#endif

    if (in > out.c_str() && in <= out.c_str() + out.length())
    {
        cstring tmp;
        if (!tmp.set(in, len))
            return false;
        if (!out.set(tmp.c_str(), tmp.length()))
            return false;
    }
    else
    {
        if (!out.set(in, len))
            return false;
    }

    in = out.c_str();
    assert(len == out.length());

    bool title_char = true;
    str_iter iter(in, len);
    while (iter.more())
    {
        const char* p = iter.get_pointer();
        const char32_t u = iter.next();

        const char c = *p;

        const bool upper = (mode == transform_mode::upper || (title_char && mode == transform_mode::title));
        if (upper)
        {
            if (c >= 'a' && c <= 'z')
                *const_cast<char*>(p) = (c - 'a' + 'A');
        }
        else
        {
            if (c >= 'A' && c <= 'Z')
                *const_cast<char*>(p) = (c - 'A' + 'a');
        }

        title_char = (u <= 0xffff && !!iswspace(uint16_t(u)));
    }

    assert(out.c_str()[len] == 0);
    return true;
}

class high_resolution_clock
{
public:
                        high_resolution_clock();
    double              elapsed() const;
private:
#ifdef _WIN32
    double              m_freq;
    int64_t             m_start;
#else
    double              m_start;
#endif
};

high_resolution_clock::high_resolution_clock()
{
#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
    if (QueryPerformanceFrequency(&freq) &&
        QueryPerformanceCounter(&start) &&
        freq.QuadPart)
    {
        m_freq = double(freq.QuadPart);
        m_start = start.QuadPart;
    }
    else
    {
        m_freq = 0;
        m_start = 0;
    }
#else
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    m_start = double(start.tv_sec) + (start.tv_nsec * 1e-9);
#endif
}

double high_resolution_clock::elapsed() const
{
#ifdef _WIN32
    if (!m_freq)
        return -1;

    LARGE_INTEGER current;
    if (!QueryPerformanceCounter(&current))
        return -1;

    const int64_t delta = current.QuadPart - m_start;
    if (delta < 0)
        return -1;

    const double result = double(delta) / m_freq;
    return result;
#else
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);

    const double now = double(current.tv_sec) + (current.tv_nsec * 1e-9);
    return now - m_start;
#endif
}

static high_resolution_clock s_clock;

double clock()
{
    return s_clock.elapsed();
}

bool getenv(const char* name, cstring& out)
{
#ifdef _WIN32
    cstring_t<WCHAR> wname;
    cstring_t<WCHAR> wout;

    out.clear();

    if (!to_utf16(name, -1, wname))
        return false;

    const DWORD needed = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (!needed)
        return false;

    if (!wout.reserve(needed))
        return false;

    const DWORD used = GetEnvironmentVariableW(wname.c_str(), wout.reserve(0), DWORD(wout.capacity()));
    if (!used)
        return false;

    wout.set_length(used);
    return to_utf8(wout.c_str(), wout.length(), out);
#else
    // TODO-LINUX: Alternative Linux implementation.
#endif
}

static bool s_test_harness = false;
void set_test_harness() { s_test_harness = true; }
bool is_test_harness() { return s_test_harness; }

} // namespace tib
