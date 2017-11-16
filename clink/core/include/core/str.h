// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
    bool                reserve(unsigned int size);
    TYPE*               data();
    const TYPE*         c_str() const;
    unsigned int        size() const;
    bool                is_growable() const;
    unsigned int        length() const;
    unsigned int        char_count() const;
    void                clear();
    bool                empty() const;
    void                truncate(unsigned int length);
    int                 first_of(int c) const;
    int                 last_of(int c) const;
    bool                equals(const TYPE* rhs) const;
    bool                iequals(const TYPE* rhs) const;
    bool                copy(const TYPE* src);
    bool                concat(const TYPE* src, int n=-1);
    bool                format(const TYPE* format, ...);
    TYPE                operator [] (unsigned int i) const;
    str_impl&           operator << (const TYPE* rhs);
    template <int I>
    str_impl&           operator << (const TYPE (&rhs)[I]);
    str_impl&           operator << (const str_impl& rhs);
    void                operator = (const str_impl&) = delete;

protected:
    void                set_growable(bool state=true);

private:
    typedef unsigned short ushort;

    void                free_data();
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
void str_impl<TYPE>::attach(TYPE* data, unsigned int size)
{
    if (is_growable())
    {
        free_data();
        m_data = data;
        m_size = size;
        m_owns_ptr = 1;
    }
    else
    {
        clear();
        concat(data, size);
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
bool str_impl<TYPE>::reserve(unsigned int new_size)
{
    if (m_size >= new_size)
        return true;

    if (!is_growable())
        return false;

    new_size = (new_size + 63) & ~63;

    TYPE* new_data = (TYPE*)malloc(new_size * sizeof(TYPE));
    memcpy(new_data, c_str(), m_size * sizeof(TYPE));

    free_data();

    m_data = new_data;
    m_size = new_size;
    m_owns_ptr = 1;
    return true;
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::free_data()
{
    if (m_owns_ptr)
        free(m_data);
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
    if (!m_length & !empty())
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
    reserve(len + n + 1);

    int remaining = m_size - len - 1;

    bool truncated = (remaining < n);
    if (!truncated)
        remaining = n;

    if (remaining > 0)
    {
        str_ncat(m_data + len, src, remaining);
        m_length += remaining;
    }

    return !truncated;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::format(const TYPE* format, ...)
{
    va_list args;
    va_start(args, format);
    unsigned int ret = vsnprint(m_data, m_size, format, args);
    va_end(args);

    m_data[m_size - 1] = '\0';
    m_length = 0;

    return (ret <= m_size);
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
    concat(rhs.c_str());
    return *this;
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
                     str_base(const str_base&&)        = delete;
    int              from_utf16(const wchar_t* utf16)  { clear(); return to_utf8(*this, utf16); }
    void             operator = (const char* value)    { copy(value); }
    void             operator = (const wchar_t* value) { from_utf16(value); }
    void             operator = (const str_base& rhs)  = delete;
};

class wstr_base : public str_impl<wchar_t>
{
public:
    template <int I> wstr_base(char (&data)[I]) : str_impl<wchar_t>(data, I) {}
                     wstr_base(wchar_t* data, int size) : str_impl<wchar_t>(data, size) {}
                     wstr_base(const wstr_base&)        = delete;
                     wstr_base(const wstr_base&&)       = delete;
    int              from_utf8(const char* utf8)        { clear(); return to_utf16(*this, utf8); }
    void             operator = (const wchar_t* value)  { copy(value); }
    void             operator = (const char* value)     { from_utf8(value); }
    void             operator = (const wstr_base&)      = delete;
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
                str(const str&&) = delete;
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
                wstr(const wstr&&) = delete;
    using       wstr_base::operator =;

private:
    wchar_t     m_data[COUNT];
};



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
