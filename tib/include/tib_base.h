// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#define NOMINMAX
#define VC_EXTRALEAN

#include <cstdint>
#include <vector>
#include <assert.h>
#include <limits>

#ifdef TIB_CONFIG_H
#include <tib_config.h>
#endif

#ifndef tib_malloc
#define tib_malloc malloc
#endif
#ifndef tib_calloc
#define tib_calloc calloc
#endif
#ifndef tib_realloc
#define tib_realloc realloc
#endif
#ifndef tib_free
#define tib_free free
#endif

namespace tib {

constexpr size_t c_auto_length = size_t(-1);

#undef min
template <class A> A min(A a, A b) { return (a < b) ? a : b; }

#undef max
template <class A> A max(A a, A b) { return (a > b) ? a : b; }

#undef clamp
template <class A> A clamp(A v, A m, A M) { return min(max(v, m), M); }

constexpr int16_t int16_min = std::numeric_limits<int16_t>::min();
constexpr int16_t int16_max = std::numeric_limits<int16_t>::max();
constexpr int32_t int32_min = std::numeric_limits<int32_t>::min();
constexpr int32_t int32_max = std::numeric_limits<int32_t>::max();

size_t resolve_auto_length(size_t len, const char* s) noexcept;
#ifdef _WIN32
size_t resolve_auto_length(size_t len, const WCHAR* s) noexcept;
#endif

typedef int32_t textpos_t;

struct coord
{
    bool                operator==(const coord& other) const { return x == other.x && y == other.y; }
    bool                operator!=(const coord& other) const { return x != other.x || y != other.y; }
    int32_t             x;
    int32_t             y;
};

// A counted string that can optionally contain embedded NUL characters.
template<class T>
class cstring_t
{
public:
                        ~cstring_t() noexcept { ::tib_free(m_text); }
                        cstring_t() noexcept = default;
                        cstring_t(const T* s, size_t len=c_auto_length) { raw_set(s, len); }
                        cstring_t(const cstring_t<T>& s) { raw_set(s.m_text, s.m_len); }
                        cstring_t(cstring_t<T>&& s) noexcept { *this = std::move(s); }
    cstring_t<T>&       operator=(const T* s);
    cstring_t<T>&       operator=(const cstring_t<T>& s);
    cstring_t<T>&       operator=(cstring_t<T>&& s) noexcept;
    bool                operator==(const T* s) const noexcept;
    bool                operator==(const cstring_t<T>& s) const noexcept;

    bool                set(const T* s, size_t len=c_auto_length);
    bool                set(const cstring_t<T>& s);
    bool                set_at(size_t index, char c) noexcept;
    bool                append(const T* s, size_t len=c_auto_length);
    bool                append_char(char c, size_t n=1);
    bool                append_spaces(size_t n);
    bool                append_color(const T* sgr_params);
    bool                delete_range(size_t index, size_t len) noexcept;
    bool                printf(const T* format, ...);
    bool                printfv(const T* format, va_list args);
    T*                  reserve(size_t len);
    void                set_length(size_t len) noexcept;
    void                clear() noexcept;
    void                free() noexcept;

    bool                empty() const noexcept { return !length(); }
    size_t              length() const noexcept;
    size_t              capacity() const noexcept { return m_capacity; }
    const T*            c_str() const noexcept;
    bool                equals(const cstring_t<T>& s) const noexcept;

private:
    bool                raw_set(const T* s, size_t len);

private:
    size_t              m_capacity = 0;
    mutable size_t      m_len = 0;
    T*                  m_text = nullptr;

    static const T* const c_spaces;
};

template<class T>
cstring_t<T>& cstring_t<T>::operator=(const T* s)
{
    set(s);
    return *this;
}

template<class T>
cstring_t<T>& cstring_t<T>::operator=(const cstring_t<T>& s)
{
    set(s.m_text, s.m_len);
    return *this;
}

template<class T>
cstring_t<T>& cstring_t<T>::operator=(cstring_t<T>&& s) noexcept
{
    ::tib_free(m_text);
    m_capacity = s.m_capacity;
    m_len = s.m_len;
    m_text = s.m_text;
    s.m_capacity = 0;
    s.m_len = 0;
    s.m_text = nullptr;
    return *this;
}

template<class T>
bool cstring_t<T>::operator==(const T* s) const noexcept
{
    size_t const len = str_len(s);
    return (length() == len) && (memcmp(m_text, s, m_len) == 0);
}

template<class T>
bool cstring_t<T>::operator==(const cstring_t<T>& s) const noexcept
{
    return equals(s);
}

template<class T>
bool cstring_t<T>::equals(const cstring_t<T>& s) const noexcept
{
    return (length() == s.length()) && (memcmp(m_text, s.m_text, m_len) == 0);
}

template<class T>
bool cstring_t<T>::set(const T* s, size_t len)
{
    return raw_set(s, len);
}

template<class T>
bool cstring_t<T>::set(const cstring_t<T>& s)
{
    return raw_set(s.c_str(), s.length());
}

template<class T>
bool cstring_t<T>::set_at(size_t index, char c) noexcept
{
    assert(index < length());
    if (index >= length())
        return false;
    m_text[index] = c;
    return true;
}

template<class T>
bool cstring_t<T>::append(const T* s, size_t len)
{
    len = resolve_auto_length(len, s);
    const size_t needed = length() + len + 1;
    if (needed >= m_capacity)
    {
        size_t grow = max<size_t>(64, m_capacity * 2);
        if (needed >= grow)
            grow = needed + (needed / 2);
        if (!reserve(grow))
            return false;
    }
    memcpy(m_text + m_len, s, len);
    m_len += len;
    m_text[m_len] = 0;
    return true;
}

template<class T>
bool cstring_t<T>::append_char(char c, size_t n)
{
    while (n > 0)
    {
        if (!append(&c, 1))
            return false;
        --n;
    }
    return true;
}

template<class T>
bool cstring_t<T>::append_spaces(size_t n)
{
    while (n > 0)
    {
        const size_t chunk_size = min<size_t>(n, 32);
        if (!append(c_spaces, chunk_size))
            return false;
        n -= chunk_size;
    }
    return true;
}

template<class T>
bool cstring_t<T>::append_color(const T* sgr_params)
{
    const size_t orig_len = length();

    if (sgr_params && sgr_params[0] == 0x1b && sgr_params[1] == '[' && sgr_params[strlen(sgr_params) - 1] == 'm')
        return append(sgr_params);

    if (!append("\x1b[", 2))
    {
nope:
        set_length(orig_len);
        return false;
    }

    if (sgr_params)
        (void)append(sgr_params);   // Intentionally ignore failure.
    if (!append("m"))
        goto nope;

    return true;
}

template<class T>
bool cstring_t<T>::delete_range(size_t index, size_t len) noexcept
{
    assert(index <= length());
    if (index > length())
        return false;
    if (index + len >= length())
    {
        set_length(index);
    }
    else
    {
        len = min(len, length() - index);
        memmove(m_text + index, m_text + (index + len), length() - (index + len));
        assert(m_len >= len);
        m_len -= len;
    }
    return true;
}

inline size_t str_len(const char* s) noexcept { return strlen(s); }
int __vsnprintf(char* buffer, size_t len, const char* format, va_list args);

#ifdef _WIN32
inline size_t str_len(const WCHAR* s) noexcept { return wcslen(s); }
int __vsnprintf(WCHAR* buffer, size_t len, const WCHAR* format, va_list args);
#endif

template<class T>
bool cstring_t<T>::printfv(const T* format, va_list args)
{
    const size_t len = length();
    size_t cap = m_capacity - len;

    int res = -1;
    if (cap > 1)
    {
        errno = 0;
        res = __vsnprintf(m_text + len, cap, format, args);
        if (errno)
        {
            m_text[len] = 0;
            return false;
        }
    }

    if (res < 0)
    {
        cap = std::max<size_t>(cap * 2, 100);
        while (res < 0)
        {
            // Subtract 1 from the max character count to ensure room for null
            // terminator; see MSDN for idiosyncracy of the snprintf family of
            // functions.
            if (!reserve(len + cap))
                return false;
            errno = 0;
            res = __vsnprintf(m_text + len, cap, format, args);
            if (errno)
            {
                m_text[len] = 0;
                return false;
            }
            cap *= 2;
        }
    }

    m_len += res;
    assert(m_len == len + str_len(m_text + len));
    assert(m_len < m_capacity);
    return true;
}

template<class T>
bool cstring_t<T>::printf(const T* format, ...)
{
    va_list args;
    va_start(args, format);
    const bool ret = printfv(format, args);
    va_end(args);
    return ret;
}

template<class T>
T* cstring_t<T>::reserve(size_t len)
{
    ++len;
    if (len >= m_capacity)
    {
        const auto old_len = length();
        T* tmp = static_cast<T*>(tib_realloc(m_text, len * sizeof(T)));
        if (!tmp)
            return nullptr;
        m_text = tmp;
        m_text[old_len] = 0;
        m_text[len - 1] = 0;
        m_capacity = len;
    }
    return m_text;
}

template<class T>
void cstring_t<T>::set_length(size_t len) noexcept
{
    if (!len)
    {
        clear();
    }
    else if (len == c_auto_length)
    {
        m_len = len;
    }
    else
    {
        assert(len < m_capacity);
        m_len = len;
        m_text[m_len] = 0;
    }
}

template<class T>
void cstring_t<T>::clear() noexcept
{
    m_len = 0;
    if (m_text && m_capacity)
        m_text[m_len] = 0;
}

template<class T>
void cstring_t<T>::free() noexcept
{
    ::tib_free(m_text);
    m_capacity = 0;
    m_len = 0;
    m_text = nullptr;
}

template<class T>
size_t cstring_t<T>::length() const noexcept
{
    if (m_len == c_auto_length)
        m_len = m_text ? str_len(m_text) : 0;
    return m_len;
}

template<class T>
bool cstring_t<T>::raw_set(const T* s, size_t len)
{
    len = resolve_auto_length(len, s);
    if (!reserve(len))
        return false;

    memcpy(m_text, s, len);
    m_len = len;
    m_text[m_len] = 0;
    return true;
}

typedef cstring_t<char> cstring;

#ifdef _WIN32
bool to_utf8(const WCHAR* s, size_t len, cstring_t<char>& out);
bool to_utf16(const char* s, size_t len, cstring_t<WCHAR>& out);
#endif

enum class transform_mode { title, upper, lower };
bool str_transform(const char* in, size_t len, cstring& out, transform_mode mode);

double clock();
bool getenv(const char* name, cstring& out);
inline bool implies(bool a, bool b) { return !a || b; }

void set_test_harness();
bool is_test_harness();

} // namespace tib
