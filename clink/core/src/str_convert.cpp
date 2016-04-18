// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str.h"
#include "str_iter.h"

//------------------------------------------------------------------------------
template <typename TYPE>
struct builder
{
                builder(TYPE* data, int max_length);
                ~builder()                            { *write = '\0'; }
    bool        truncated() const                     { return (write >= end); }
    int         get_written() const                   { return int(write - start); }
    builder&    operator << (int value);
    TYPE*       write;
    const TYPE* start;
    const TYPE* end;
};

//------------------------------------------------------------------------------
template <typename TYPE>
builder<TYPE>::builder(TYPE* data, int max_length)
: write(data)
, start(data)
, end(data + max_length - 1)
{
}

//------------------------------------------------------------------------------
template <>
builder<wchar_t>& builder<wchar_t>::operator << (int value)
{
    // For code points that don't fit in wchar_t there is 'surrogate pairs'.
    if (value > 0xffff)
        return *this << ((value >> 10) + 0xd7c0) << ((value & 0x3ff) + 0xdc00);

    if (write < end)
        *write++ = wchar_t(value);

    return *this;
}

//------------------------------------------------------------------------------
template <>
builder<char>& builder<char>::operator << (int value)
{
    if (write < end)
        *write++ = char(value);

    return *this;
}



//------------------------------------------------------------------------------
int to_utf8(char* out, int max_count, wstr_iter& iter)
{
    builder<char> builder(out, max_count);

    int c;
    while (!builder.truncated() && (c = iter.next()))
    {
        if (c < 0x80)
        {
            builder << c;
            continue;
        }

        // How long is this encoding?
        int n = 2;
        n += c >= (1 << 11);
        n += c >= (1 << 16);

        // Collect the bytes of the encoding.
        char out_chars[4];
        switch (n)
        {
        case 4: out_chars[3] = (c & 0x3f); c >>= 6;
        case 3: out_chars[2] = (c & 0x3f); c >>= 6;
        case 2: out_chars[1] = (c & 0x3f); c >>= 6;
                out_chars[0] = (c & 0x1f) | (0xfc0 >> (n - 2));
        }

        for (int i = 0; i < n; ++i)
            builder << (out_chars[i] | 0x80);
    }

    return builder.get_written();
}

//------------------------------------------------------------------------------
int to_utf8(char* out, int max_count, const wchar_t* utf16)
{
    wstr_iter iter(utf16);
    return to_utf8(out, max_count, iter);
}

//------------------------------------------------------------------------------
int to_utf8(str_base& out, str_iter_impl<wchar_t>& utf16)
{
    int length = out.length();

    if (out.is_growable())
        out.reserve(length + utf16.length());

    return to_utf8(out.data() + length, out.size() - length, utf16);
}

//------------------------------------------------------------------------------
int to_utf8(str_base& out, const wchar_t* utf16)
{
    wstr_iter iter(utf16);
    return to_utf8(out, iter);
}



//------------------------------------------------------------------------------
int to_utf16(wchar_t* out, int max_count, str_iter& iter)
{
    builder<wchar_t> builder(out, max_count);

    int c;
    while (!builder.truncated() && (c = iter.next()))
        builder << c;

    return builder.get_written();
}

//------------------------------------------------------------------------------
int to_utf16(wchar_t* out, int max_count, const char* utf8)
{
    str_iter iter(utf8);
    return to_utf16(out, max_count, iter);
}

//------------------------------------------------------------------------------
int to_utf16(wstr_base& out, str_iter_impl<char>& utf8)
{
    int length = out.length();

    if (out.is_growable())
        out.reserve(length + utf8.length());

    return to_utf16(out.data() + length, out.size() - length, utf8);
}

//------------------------------------------------------------------------------
int to_utf16(wstr_base& out, const char* utf8)
{
    str_iter iter(utf8);
    return to_utf16(out, iter);
}
