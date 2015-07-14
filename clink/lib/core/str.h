// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <stdarg.h>
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
inline const wchar_t* str_chr(const wchar_t* s, int c)               { return wcschr(s, c); }
inline const char*    str_rchr(const char* s, int c)                 { return strrchr(s, c); }
inline const wchar_t* str_rchr(const wchar_t* s, int c)              { return wcsrchr(s, c); }

//------------------------------------------------------------------------------
template <typename TYPE>
class str_impl
{
public:
    typedef TYPE    char_t;

                    str_impl(TYPE* data, unsigned int size);
    TYPE*           data();
    const TYPE*     c_str() const;
    unsigned int    size() const;
    unsigned int    length() const;
    unsigned int    char_count() const;
    void            clear();
    bool            empty() const;
    void            truncate(unsigned int length);
    int             first_of(int c) const;
    int             last_of(int c) const;
    bool            equals(const TYPE* rhs) const;
    bool            iequals(const TYPE* rhs) const;
    bool            copy(const TYPE* src);
    bool            concat(const TYPE* src, int n=-1);
    bool            format(const TYPE* format, ...);
    TYPE            operator [] (unsigned int i) const;
    str_impl&       operator << (const TYPE* rhs);
    str_impl&       operator << (const str_impl& rhs);

private:
    TYPE*           m_data;
    unsigned int    m_size;
};

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>::str_impl(TYPE* data, unsigned int size)
: m_data(data)
, m_size(size)
{
    clear();
}

//------------------------------------------------------------------------------
template <typename TYPE>
TYPE* str_impl<TYPE>::data()
{
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
unsigned int str_impl<TYPE>::length() const
{
    return str_len(m_data);
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::clear()
{
    m_data[0] = '\0';
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::empty() const
{
    return (m_data[0] == '\0');
}

//------------------------------------------------------------------------------
template <typename TYPE>
void str_impl<TYPE>::truncate(unsigned int length)
{
    if (length < m_size)
        m_data[length] = '\0';
}

//------------------------------------------------------------------------------
template <typename TYPE>
int str_impl<TYPE>::first_of(int c) const
{
    const TYPE* r = str_chr(m_data, c);
    return r ? int(r - m_data) : -1;
}

//------------------------------------------------------------------------------
template <typename TYPE>
int str_impl<TYPE>::last_of(int c) const
{
    const TYPE* r = str_rchr(m_data, c);
    return r ? int(r - m_data) : -1;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::equals(const TYPE* rhs) const
{
    return (str_cmp(m_data, rhs) == 0);
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::iequals(const TYPE* rhs) const
{
    return (str_icmp(m_data, rhs) == 0);
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

    int m = m_size - length() - 1;

    bool truncated = false;
    if (n >= 0)
    {
        truncated = (m < n);
        m = truncated ? m : n;
    }
    else
        truncated = (str_len(src) > m);

    if (m > 0)
        str_ncat(m_data, src, m);

    return !truncated;
}

//------------------------------------------------------------------------------
template <typename TYPE>
bool str_impl<TYPE>::format(const TYPE* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprint(m_data, m_size, format, args);
    m_data[m_size - 1] = '\0';
    va_end(args);
    return (ret >= 0);
}

//------------------------------------------------------------------------------
template <typename TYPE>
TYPE str_impl<TYPE>::operator [] (unsigned int i) const
{
    return (i < length()) ? m_data[i] : 0;
}

//------------------------------------------------------------------------------
template <typename TYPE>
str_impl<TYPE>& str_impl<TYPE>::operator << (const TYPE* rhs)
{
    concat(rhs);
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
bool convert(class str_base& out, const wchar_t* utf16);
bool convert(char* out, int max_count, const wchar_t* utf16);
bool convert(class wstr_base& out, const char* utf8);
bool convert(wchar_t* out, int max_count, const char* utf8);



//------------------------------------------------------------------------------
class str_base : public str_impl<char>
{
public:
         str_base(char* data, int size) : str_impl<char>(data, size) {}
    bool convert(const wchar_t* utf16)     { clear(); return ::convert(*this, utf16); }
    void operator = (const char* value)    { copy(value); }
    void operator = (const wchar_t* value) { convert(value); }
};

class wstr_base : public str_impl<wchar_t>
{
public:
         wstr_base(wchar_t* data, int size) : str_impl<wchar_t>(data, size) {}
    bool convert(const char* utf8)          { clear(); return ::convert(*this, utf8); }
    void operator = (const wchar_t* value)  { copy(value); }
    void operator = (const char* value)     { convert(value); }
};



//------------------------------------------------------------------------------
template <int COUNT=128>
class str : public str_base
{
    char    m_data[COUNT];

public:
            str() : str_base(m_data, COUNT)     {}
            str(const char* value) : str()      { copy(value); }
            str(const wchar_t* value) : str()   { convert(value); }
    using   str_base::operator =;
};

template <int COUNT=128>
class wstr : public wstr_base
{
    wchar_t m_data[COUNT];

public:
            wstr() : wstr_base(m_data, COUNT)   {}
            wstr(const wchar_t* value) : wstr() { copy(value); }
            wstr(const char* value) : wstr()    { convert(value); }
    using   wstr_base::operator =;
};



//------------------------------------------------------------------------------
template <>
inline unsigned int str_impl<char>::char_count() const
{
    int count = 0;
    const char* ptr = c_str();
    while (int c = *ptr++)
        // 'count' is increased if the top two MSBs of c are not 10xxxxxx
        count += (c & 0xc0) != 0x80;

    return count;
}

template <>
inline unsigned int str_impl<wchar_t>::char_count() const
{
    int count = 0;
    const wchar_t* ptr = c_str();
    while (unsigned short c = *ptr++)
    {
        ++count;

        if ((c & 0xfc00) == 0xd800)
            if ((*ptr & 0xfc00) == 0xdc00)
                ++ptr;
    }

    return count;
}
