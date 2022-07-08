// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <utility>

#include <assert.h>

//------------------------------------------------------------------------------
inline int  str_len(const char* s)                                   { return int(strlen(s)); }
inline int  str_len(const wchar_t* s)                                { return int(wcslen(s)); }
inline void str_ncat(char* d, const char* s, size_t n)               { strncat(d, s, n); }
inline void str_ncat(wchar_t* d, const wchar_t* s, size_t n)         { wcsncat(d, s, n); }
inline int  str_cmp(const char* l, const char* r)                    { return strcmp(r, l); }
inline int  str_cmp(const wchar_t* l, const wchar_t* r)              { return wcscmp(r, l); }
inline int  str_icmp(const char* l, const char* r)                   { return stricmp(r, l); }
inline int  str_icmp(const wchar_t* l, const wchar_t* r)             { return wcsicmp(r, l); }
inline int  vsnprint(char* d, int n, const char* f, va_list a)       { return vsnprintf(d, n, f, a); }
inline int  vsnprint(wchar_t* d, int n, const wchar_t* f, va_list a) { return _vsnwprintf(d, n, f, a); }
inline const char*    str_chr(const char* s, int c)                  { return strchr(s, c); }
inline const wchar_t* str_chr(const wchar_t* s, int c)               { return wcschr(s, wchar_t(c)); }
inline const char*    str_rchr(const char* s, int c)                 { return strrchr(s, c); }
inline const wchar_t* str_rchr(const wchar_t* s, int c)              { return wcsrchr(s, wchar_t(c)); }

unsigned int char_count(const char*);
unsigned int char_count(const wchar_t*);

//------------------------------------------------------------------------------
template <typename TYPE>
class str_impl
{
public:
    typedef TYPE        char_t;

                        str_impl(TYPE* data, unsigned int size);
                        str_impl(const str_impl&) = delete;
                        ~str_impl();
    void                attach(TYPE* data, unsigned int size);
    bool                reserve(unsigned int size, bool exact = false);
    TYPE*               data();
    const TYPE*         c_str() const;
    unsigned int        size() const;
    bool                is_growable() const;
    unsigned int        length() const;
    unsigned int        char_count() const;
    void                clear();
    bool                empty() const;
    void                truncate(unsigned int length);
    void                trim();
    int                 first_of(int c) const;
    int                 last_of(int c) const;
    bool                equals(const TYPE* rhs) const;
    bool                iequals(const TYPE* rhs) const;
    bool                copy(const TYPE* src);
    bool                concat(const TYPE* src, int n=-1);
    bool                concat_no_truncate(const TYPE* src, int n);
    bool                format(const TYPE* format, ...);
    bool                vformat(const TYPE* format, va_list args);
    TYPE                operator [] (unsigned int i) const;
    str_impl&           operator << (const TYPE* rhs);
    template <int I>
    str_impl&           operator << (const TYPE (&rhs)[I]);
    str_impl&           operator << (const str_impl& rhs);
    void                operator = (const str_impl&) = delete;

protected:
                        str_impl(str_impl&&);
    void                set_growable(bool state=true);
    str_impl&           operator = (str_impl&&);
    bool                owns_ptr() const;
    void                reset_not_owned(TYPE* data, unsigned int size);
    void                free_data();

private:
    typedef unsigned short ushort;

    TYPE*               m_data;
    ushort              m_size : 15;
    ushort              m_growable : 1;
    mutable ushort      m_length : 15;
    ushort              m_owns_ptr : 1;
};

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>::str_impl(TYPE* data, unsigned int size)
: m_data(data)
, m_size(size)
, m_growable(0)
, m_owns_ptr(0)
, m_length(0)
{
}

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>::~str_impl()
{
    free_data();
}

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>::str_impl(str_impl&& s)
{
    *this = std::move(s);
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::attach(TYPE* data, unsigned int size)
{
    if (is_growable())
    {
        free_data();
        m_data = data;
        m_size = size;
        m_owns_ptr = 1;
        m_length = 0;
    }
    else
    {
        clear();
        concat(data, size);
        free(data);
    }
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::set_growable(bool state)
{
    m_growable = state ? 1 : 0;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::reserve(unsigned int new_size, bool exact)
{
    if (!exact)
        new_size++;

    if (m_size >= new_size)
        return true;

    if (!is_growable())
        return false;

    if (!exact)
        new_size = (new_size + 63) & ~63;

    const int old_size = m_size;
    m_size = new_size;
    if (m_size != new_size)
    {
        // Overflow!
        m_size = old_size;
        return false;
    }

    TYPE* new_data = (TYPE*)malloc(new_size * sizeof(TYPE));
    memcpy(new_data, c_str(), old_size * sizeof(TYPE));

    free_data();

    m_data = new_data;
    m_owns_ptr = 1;
    return true;
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::free_data()
{
#if defined(__MINGW32__) || defined(__MINGW64__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object" /* gcc fails to account for m_owns_ptr */
#endif

    if (m_owns_ptr)
        free(m_data);

#if defined(__MINGW32__) || defined(__MINGW64__)
#pragma GCC diagnostic pop
#endif
}

//------------------------------------------------------------------------------
template <typename TYPE>
TYPE* str_impl<TYPE>::data()
{
    m_length = 0;
    return m_data;
}

//------------------------------------------------------------------------------
template <typename TYPE>
const TYPE* str_impl<TYPE>::c_str() const
{
    return m_data;
}

//------------------------------------------------------------------------------
template <typename TYPE>
unsigned int str_impl<TYPE>::size() const
{
    return m_size;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::is_growable() const
{
    return m_growable;
}

//------------------------------------------------------------------------------
template <typename TYPE>
unsigned int str_impl<TYPE>::length() const
{
    if (!m_length)
        m_length = str_len(c_str());

    return m_length;
}

//------------------------------------------------------------------------------
template <typename TYPE>
unsigned int str_impl<TYPE>::char_count() const
{
    return ::char_count(c_str());
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::clear()
{
    m_data[0] = '\0';
    m_length = 0;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::empty() const
{
    return (c_str()[0] == '\0');
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::truncate(unsigned int pos)
{
    if (pos >= m_size)
        return;

    m_data[pos] = '\0';
    m_length = pos;
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::trim()
{
    TYPE* pos = m_data;
    TYPE* end = m_data + length();

    while (end > pos)
    {
        if (end[-1] != ' ' && end[-1] != '\t')
            break;
        end--;
    }

    if (end < m_data + m_size)
    {
        *end = '\0';
        m_length = end - pos;
    }

    while (pos < end)
    {
        if (pos[0] != ' ' && pos[0] != '\t')
            break;
        pos++;
    }

    if (pos > m_data)
    {
        m_length -= ushort(pos - m_data);
        memmove(m_data, pos, (m_length + 1) * sizeof(m_data[0]));
    }
}

//------------------------------------------------------------------------------
template <typename TYPE>
int str_impl<TYPE>::first_of(int c) const
{
    const TYPE* r = str_chr(c_str(), c);
    return r ? int(r - c_str()) : -1;
}

//------------------------------------------------------------------------------
template <typename TYPE>
int str_impl<TYPE>::last_of(int c) const
{
    const TYPE* r = str_rchr(c_str(), c);
    return r ? int(r - c_str()) : -1;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::equals(const TYPE* rhs) const
{
    return (str_cmp(c_str(), rhs) == 0);
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::iequals(const TYPE* rhs) const
{
    return (str_icmp(c_str(), rhs) == 0);
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::copy(const TYPE* src)
{
    clear();
    return concat(src);
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::concat(const TYPE* src, int n)
{
    if (src == nullptr)
        return false;

    if (n < 0)
        n = str_len(src);

    int len = length();
    reserve(len + n);

    int remaining = m_size - len - 1;

    bool truncated = (remaining < n);
    if (!truncated)
        remaining = n;

    if (remaining > 0)
    {
        // Egad.  Mingw chooses the (&rhs)[I] variant of operator<< for:
        //      char buf[10] = {'x'};
        //      code << buf;
        // So always be safe and stop copying at a NUL character.
        //
        // Callers who need NUL characters included (like translated key
        // sequence chords, e.g. ^@) must use concat_no_truncate() instead.
        TYPE* dst = m_data + len;
        while (remaining-- && *src)
            *(dst++) = *(src++);
        m_length = static_cast<unsigned int>(dst - m_data);
        m_data[m_length] = '\0';
    }

    return !truncated;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::concat_no_truncate(const TYPE* src, int n)
{
    if (src == nullptr)
        return false;
    if (n < 0)
        return false;
    if (n == 0)
        return true;

    int len = length();
    reserve(len + n);

    int remaining = m_size - len - 1;
    if (remaining < n)
        return false;

    memcpy(m_data + len, src, n * sizeof(TYPE));
    m_length += n;
    m_data[m_length] = '\0';
    return true;
}

//------------------------------------------------------------------------------
template <>
inline bool str_impl<char>::format(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int len = vsnprint(m_data, m_size, format, args);
    assert(len >= 0); // Compiler should comply with spec.
    if (len >= int(m_size) && reserve(len))
        len = vsnprint(m_data, m_size, format, args);
    va_end(args);

    m_data[m_size - 1] = '\0';
    m_length = 0;

    return ((unsigned int)len <= m_size);
}

//------------------------------------------------------------------------------
template <>
inline bool str_impl<char>::vformat(const char* format, va_list args)
{
    int len = vsnprint(m_data, m_size, format, args);
    assert(len >= 0); // Compiler should comply with spec.
    if (len >= int(m_size) && reserve(len))
        len = vsnprint(m_data, m_size, format, args);

    m_data[m_size - 1] = '\0';
    m_length = 0;

    return ((unsigned int)len <= m_size);
}

//------------------------------------------------------------------------------
template <>
inline bool str_impl<wchar_t>::format(const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);

    int len = vsnprint(m_data, m_size - 1, format, args);
    if (len < 0 && is_growable())
    {
        // _vsnwprintf works differently and only indicates how much space is
        // needed if explicitly asked.
        len = vsnprint(nullptr, 0, format, args);
        if (len >= 0 && reserve(len))
            len = vsnprint(m_data, m_size - 1, format, args);
    }
    va_end(args);

    m_data[m_size - 1] = '\0';
    m_length = 0;

    return ((unsigned int)len <= m_size);
}

//------------------------------------------------------------------------------
template <>
inline bool str_impl<wchar_t>::vformat(const wchar_t* format, va_list args)
{
    int len = vsnprint(m_data, m_size - 1, format, args);
    if (len < 0 && is_growable())
    {
        // _vsnwprintf works differently and only indicates how much space is
        // needed if explicitly asked.
        len = vsnprint(nullptr, 0, format, args);
        if (len >= 0 && reserve(len))
            len = vsnprint(m_data, m_size - 1, format, args);
    }

    m_data[m_size - 1] = '\0';
    m_length = 0;

    return ((unsigned int)len <= m_size);
}

//------------------------------------------------------------------------------
template <typename TYPE>
TYPE str_impl<TYPE>::operator [] (unsigned int i) const
{
    return (i < length()) ? c_str()[i] : 0;
}

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>& str_impl<TYPE>::operator << (const TYPE* rhs)
{
    concat(rhs);
    return *this;
}

//------------------------------------------------------------------------------
template <typename TYPE> template <int I>
str_impl<TYPE>& str_impl<TYPE>::operator << (const TYPE (&rhs)[I])
{
    concat(rhs, I);
    return *this;
}

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>& str_impl<TYPE>::operator << (const str_impl& rhs)
{
    concat_no_truncate(rhs.c_str(), rhs.length());
    return *this;
}

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>& str_impl<TYPE>::operator = (str_impl&& s)
{
    assert(s.m_owns_ptr); // Otherwise our m_data will point into s.

    free_data();

    memcpy(this, &s, sizeof(*this));

    // This leaves s in a non-reusable state!  But s can be repaired by using
    // reset_not_owned().
    s.m_data = nullptr;
    s.m_size = 0;
    s.m_length = 0;
    s.m_owns_ptr = false;

    return *this;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::owns_ptr() const
{
    return m_owns_ptr;
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::reset_not_owned(TYPE* data, unsigned int size)
{
    // reset_data() can be used to repair this back into a reusable state after
    // invoking operator=(str_impl&&).
    m_data = data;
    m_size = size;
    m_owns_ptr = false;
    m_length = 0;
}



//------------------------------------------------------------------------------
template <typename T> class str_iter_impl;

int to_utf8(class str_base& out, const wchar_t* utf16);
int to_utf8(class str_base& out, str_iter_impl<wchar_t>& iter);
int to_utf8(char* out, int max_count, const wchar_t* utf16);
int to_utf8(char* out, int max_count, str_iter_impl<wchar_t>& iter);

int to_utf16(class wstr_base& out, const char* utf8);
int to_utf16(class wstr_base& out, str_iter_impl<char>& iter);
int to_utf16(wchar_t* out, int max_count, const char* utf8);
int to_utf16(wchar_t* out, int max_count, str_iter_impl<char>& iter);



//------------------------------------------------------------------------------
class str_base : public str_impl<char>
{
public:
    template <int I> str_base(char (&data)[I]) : str_impl<char>(data, I) {}
                     str_base(char* data, int size) : str_impl<char>(data, size) {}
                     str_base(const str_base&)         = delete;
    int              from_utf16(const wchar_t* utf16)  { clear(); return to_utf8(*this, utf16); }
    void             operator = (const char* value)    { copy(value); }
    void             operator = (const wchar_t* value) { from_utf16(value); }
    void             operator = (const str_base& rhs)  = delete;
    str_base&        operator = (str_base&& rhs)       { str_impl<char>::operator=(std::move(rhs)); return *this; }

protected:
                     str_base(str_base&&)              = default;
};

class wstr_base : public str_impl<wchar_t>
{
public:
    template <int I> wstr_base(wchar_t (&data)[I]) : str_impl<wchar_t>(data, I) {}
                     wstr_base(wchar_t* data, int size) : str_impl<wchar_t>(data, size) {}
                     wstr_base(const wstr_base&)        = delete;
    int              from_utf8(const char* utf8)        { clear(); return to_utf16(*this, utf8); }
    using            str_impl<wchar_t>::operator =;
    void             operator = (const wchar_t* value)  { copy(value); }
    void             operator = (const char* value)     { from_utf8(value); }
    void             operator = (const wstr_base&)      = delete;
    wstr_base&       operator = (wstr_base&& rhs)       { str_impl<wchar_t>::operator=(std::move(rhs)); return *this; }

protected:
                     wstr_base(wstr_base&&)             = default;
};



//------------------------------------------------------------------------------
template <int COUNT=128, bool GROWABLE=true>
class str : public str_base
{
public:
                str() : str_base(m_data, COUNT)     { clear(); set_growable(GROWABLE); }
    explicit    str(const char* value) : str()      { copy(value); }
    explicit    str(const wchar_t* value) : str()   { from_utf16(value); }
                str(const str&) = delete;
                str(str&&) = delete;
    using       str_base::operator =;

private:
    char        m_data[COUNT];
};

template <int COUNT=128, bool GROWABLE=true>
class wstr : public wstr_base
{
public:
                wstr() : wstr_base(m_data, COUNT)   { clear(); set_growable(GROWABLE); }
    explicit    wstr(const wchar_t* value) : wstr() { copy(value); }
    explicit    wstr(const char* value) : wstr()    { from_utf8(value); }
                wstr(const wstr&) = delete;
                wstr(wstr&&) = delete;
    using       wstr_base::operator =;

private:
    wchar_t     m_data[COUNT];
};



//------------------------------------------------------------------------------
// Ugh!  I'll accept the size inefficiency of having a non-static m_empty member
// to retain the general case runtime performance efficiency of str_impl being
// able to assume m_data is always a writable buffer.  Ideally there would be a
// way to have a single global const empty string shared by all class instances,
// without adding a vtbl.
class str_moveable : public str_base
{
public:
                str_moveable() : str_base(m_empty, 1)               { clear(); set_growable(); }
    explicit    str_moveable(const char* value) : str_moveable()    { copy(value); }
    explicit    str_moveable(const wchar_t* value) : str_moveable() { from_utf16(value); }
                str_moveable(const str_moveable&) = delete; // (Did you forget const& in a for range loop?)
                str_moveable(str_moveable&& s) : str_moveable()     { *this = std::move(s); }
    using       str_base::operator =;
    str_moveable& operator = (str_moveable&&);

    char*       detach();
    void        free();

private:
    char        m_empty[1];
};

class wstr_moveable : public wstr_base
{
public:
                wstr_moveable() : wstr_base(m_empty, 1)             { clear(); set_growable(); }
    explicit    wstr_moveable(const wchar_t* value) : wstr_moveable() { copy(value); }
    explicit    wstr_moveable(const char* value) : wstr_moveable()  { from_utf8(value); }
                wstr_moveable(const wstr_moveable&) = delete; // (Did you forget const& in a for range loop?)
                wstr_moveable(wstr_moveable&& s) : wstr_moveable()  { *this = std::move(s); }
    using       wstr_base::operator =;
    wstr_moveable& operator = (wstr_moveable&&);

    wchar_t*    detach();
    void        free();

protected:
    wchar_t     m_empty[1];
};

//------------------------------------------------------------------------------
inline str_moveable& str_moveable::operator = (str_moveable&& s)
{
    if (s.owns_ptr())
    {
        str_base::operator=(std::move(s));
        s.reset_not_owned(s.m_empty, _countof(s.m_empty));
    }
    else
    {
        clear();
    }
    return *this;
}

//------------------------------------------------------------------------------
inline char* str_moveable::detach()
{
    char* s = data();
    reset_not_owned(m_empty, _countof(m_empty));
    return s;
}

//------------------------------------------------------------------------------
inline void str_moveable::free()
{
    attach(nullptr, 0);
    reset_not_owned(m_empty, _countof(m_empty));
}

//------------------------------------------------------------------------------
inline wstr_moveable& wstr_moveable::operator = (wstr_moveable&& s)
{
    if (s.owns_ptr())
    {
        wstr_base::operator=(std::move(s));
        s.reset_not_owned(s.m_empty, _countof(s.m_empty));
    }
    else
    {
        clear();
    }
    return *this;
}

//------------------------------------------------------------------------------
inline wchar_t* wstr_moveable::detach()
{
    wchar_t* s = data();
    reset_not_owned(m_empty, _countof(m_empty));
    return s;
}

//------------------------------------------------------------------------------
inline void wstr_moveable::free()
{
    attach(nullptr, 0);
    reset_not_owned(m_empty, _countof(m_empty));
}



//------------------------------------------------------------------------------
template <typename T> void concat_strip_quotes(str_impl<T>& out, const T* in, unsigned int len=-1)
{
    if (len == static_cast<unsigned int>(-1))
        len = static_cast<unsigned int>(strlen(in));

    // Strip quotes while concatenating.  This may seem surprising, but it's a
    // technique lifted from CMD, and it works well.
    out.reserve(out.length() + len);
    while (len)
    {
        const T* append = in;
        while (len)
        {
            if (*in == '"')
                break;
            in++;
            len--;
        }

        out.concat(append, static_cast<unsigned int>(in - append));

        while (len && *in == '"')
        {
            in++;
            len--;
        }
    }
}

//------------------------------------------------------------------------------
inline unsigned int char_count(const char* ptr)
{
    unsigned int count = 0;
    while (int c = *ptr++)
        // 'count' is increased if the top two MSBs of c are not 10xxxxxx
        count += (c & 0xc0) != 0x80;

    return count;
}

inline unsigned int char_count(const wchar_t* ptr)
{
    unsigned int count = 0;
    while (unsigned short c = *ptr++)
    {
        ++count;

        if ((c & 0xfc00) == 0xd800)
            if ((*ptr & 0xfc00) == 0xdc00)
                ++ptr;
    }

    return count;
}
