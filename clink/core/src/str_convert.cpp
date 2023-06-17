// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str.h"
#include "str_iter.h"

#ifdef USE_OS_UTF_CONVERSION
#include <assert.h>
#endif

//------------------------------------------------------------------------------
template <typename TYPE>
struct builder
{
                builder(TYPE* data, int32 max_length);
                ~builder()                            { if (start && start <= end) *write = '\0'; }
    bool        truncated() const                     { return (start && write >= end); }
    int32       get_written() const                   { return int32(write - start); }
    builder&    operator << (int32 value);
    TYPE*       write;
    const TYPE* start;
    const TYPE* end;
};

//------------------------------------------------------------------------------
template <typename TYPE>
builder<TYPE>::builder(TYPE* data, int32 max_length)
: write(data)
, start(data)
, end(data + max_length - 1)
{
}

//------------------------------------------------------------------------------
template <>
builder<wchar_t>& builder<wchar_t>::operator << (int32 value)
{
    // For code points that don't fit in wchar_t there is 'surrogate pairs'.
    if (value > 0xffff)
        return *this << ((value >> 10) + 0xd7c0) << ((value & 0x3ff) + 0xdc00);

    if (!start)
        write++;
    else if (write < end)
        *write++ = wchar_t(value);

    return *this;
}

//------------------------------------------------------------------------------
template <>
builder<char>& builder<char>::operator << (int32 value)
{
    if (!start)
        write++;
    else if (write < end)
        *write++ = char(value);

    return *this;
}



//------------------------------------------------------------------------------
int32 to_utf8(char* out, int32 max_count, wstr_iter& iter)
{
    // See to_utf16() for explanation about the disabled WideCharToMultiByte
    // implementation below.
#ifdef USE_OS_UTF_CONVERSION
    // First try to use the OS function because it's fast.  Leave room to
    // terminate the string.

    int32 len = WideCharToMultiByte(CP_UTF8, 0, iter.get_pointer(), iter.length(), out, max<int32>(max_count - 1, 0), nullptr, nullptr);
    if (!out || !max_count)
    {
        iter.reset_pointer(iter.get_pointer() + iter.length());
        return len;
    }

    if (len || !iter.length())
    {
        assert(len < max_count); // Should be guaranteed by max_count - 1 above.
        out[len] = '\0';
        iter.reset_pointer(iter.get_pointer() + iter.length());
        return len;
    }

    // Some error occurred.  Use our own implementation because it can convert
    // partial strings into a non-growable buffer.
#endif

    // BUGBUG:  Why is truncation desirable?  Truncating a display-only string
    // might be ok, but truncating any other string can result in dangerously
    // incorrect behavior.  But there is a test in test/str_convert.cpp, so
    // fall back to this for now.

    builder<char> builder(out, max_count);

    int32 c;
    while (!builder.truncated() && (c = iter.next()))
    {
        if (c < 0x80)
        {
            builder << c;
            continue;
        }

        // How long is this encoding?
        int32 n = 2;
        n += c >= (1 << 11);
        n += c >= (1 << 16);

        // Collect the bytes of the encoding.
        char out_chars[4];
        switch (n)
        {
        case 4: out_chars[3] = (c & 0x3f); c >>= 6;
        case 3: out_chars[2] = (c & 0x3f); c >>= 6;
        case 2: out_chars[1] = (c & 0x3f); c >>= 6;
                out_chars[0] = (c & 0x1f) | char(0xfc0 >> (n - 2));
        }

        for (int32 i = 0; i < n; ++i)
            builder << (out_chars[i] | 0x80);
    }

    return builder.get_written();
}

//------------------------------------------------------------------------------
int32 to_utf8(char* out, int32 max_count, const wchar_t* utf16)
{
    wstr_iter iter(utf16);
    return to_utf8(out, max_count, iter);
}

//------------------------------------------------------------------------------
int32 to_utf8(str_base& out, str_iter_impl<wchar_t>& utf16)
{
    int32 length = out.length();

    if (out.is_growable())
    {
        str_iter_impl<wchar_t> calc_needed(utf16.get_pointer(), utf16.length());
        int32 needed = to_utf8(nullptr, 0, calc_needed);
        out.reserve(length + needed);
    }

    return to_utf8(out.data() + length, out.size() - length, utf16);
}

//------------------------------------------------------------------------------
int32 to_utf8(str_base& out, const wchar_t* utf16)
{
    wstr_iter iter(utf16);
    return to_utf8(out, iter);
}



//------------------------------------------------------------------------------
int32 to_utf16(wchar_t* out, int32 max_count, str_iter& iter)
{
    // This function was making the test program very slow in debug builds.  But
    // that was because the match pipeline was sorting during generation even
    // though the order didn't matter until they're displayed.  Fixing that
    // resolved the reason for changing this to use MultiByteToWideChar, and in
    // optimized builds this function is nearly as fast as MultiByteToWideChar.
    // So to reduce risk of regression this new code is disabled for now.
#ifdef USE_OS_UTF_CONVERSION
    // First try to use the OS function because it's faster.  Leave room to
    // terminate the string.

    int32 len = MultiByteToWideChar(CP_UTF8, 0, iter.get_pointer(), iter.length(), out, max<int32>(max_count - 1, 0));
    if (!out || !max_count)
    {
        iter.reset_pointer(iter.get_pointer() + iter.length());
        return len;
    }

    if (len || !iter.length())
    {
        assert(len < max_count); // Should be guaranteed by max_count - 1 above.
        out[len] = '\0';
        iter.reset_pointer(iter.get_pointer() + iter.length());
        return len;
    }

    // Some error occurred.  Use our own implementation because it can convert
    // partial strings into a non-growable buffer.
#endif

    // BUGBUG:  Why is truncation desirable?  Truncating a display-only string
    // might be ok, but truncating any other string can result in dangerously
    // incorrect behavior.  But there is a test in test/str_convert.cpp, so fall
    // back to this for now.

    builder<wchar_t> builder(out, max_count);

    int32 c;
    while (!builder.truncated() && (c = iter.next()))
        builder << c;

    return builder.get_written();
}

//------------------------------------------------------------------------------
int32 to_utf16(wchar_t* out, int32 max_count, const char* utf8)
{
    str_iter iter(utf8);
    return to_utf16(out, max_count, iter);
}

//------------------------------------------------------------------------------
int32 to_utf16(wstr_base& out, str_iter_impl<char>& utf8)
{
    int32 length = out.length();

    if (out.is_growable())
    {
        str_iter_impl<char> calc_needed(utf8.get_pointer(), utf8.length());
        int32 needed = to_utf16(nullptr, 0, calc_needed);
        out.reserve(length + needed);
    }

    return to_utf16(out.data() + length, out.size() - length, utf8);
}

//------------------------------------------------------------------------------
int32 to_utf16(wstr_base& out, const char* utf8)
{
    str_iter iter(utf8);
    return to_utf16(out, iter);
}
