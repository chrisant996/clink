/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

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
                    str_impl(TYPE* data, unsigned int size);
                    str_impl(const TYPE* rhs);
    TYPE*           data();
    unsigned int    size() const;
    unsigned int    length() const;
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
                    operator const TYPE* () const;

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
str_impl<TYPE>::operator const TYPE* () const
{
    return m_data;
}



//------------------------------------------------------------------------------
class str_base : public str_impl<char>
{
public:
    str_base(char* data, unsigned int size) : str_impl<char>(data, size) {}
};

class wstr_base : public str_impl<wchar_t>
{
public:
    wstr_base(wchar_t* data, unsigned int size) : str_impl<wchar_t>(data, size) {}
};



//------------------------------------------------------------------------------
template <int COUNT=128>
class str : public str_base
{
    char        m_data[COUNT];

public:
                str() : str_base(m_data, COUNT) {}
};

template <int COUNT=128>
class wstr : public wstr_base
{
    wchar_t     m_data[COUNT];

public:
                wstr() : wstr_base(m_data, COUNT) {}
};
