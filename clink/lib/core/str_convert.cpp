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

#include "pch.h"
#include "str.h"

//------------------------------------------------------------------------------
template <typename TYPE>
struct builder
{
    builder(TYPE* data, int max_length)
    : write(data)
    , end(data + max_length - 1)
    {
    }

    ~builder()
    {
        *write = '\0';
    }

    bool truncated() const
    {
        return (write > end);
    }

    builder& operator << (int value)
    {
        // For code points that don't fit in wchar_t there is 'surrogate pairs'.
        if (value > 0xffff)
            return *this << ((value >> 10) + 0xd7c0) << ((value & 0x3ff) + 0xdc00);

        if (write < end)
            *write++ = TYPE(value);

        return *this;
    }

    TYPE*           write;
    const TYPE*     end;
};



//------------------------------------------------------------------------------
bool convert(char* out, int max_count, const wchar_t* utf16)
{
    builder<char> builder(out, max_count);

    while (unsigned int c = *utf16++)
    {
        if (c < 0x80)
        {
            builder << c;
            continue;
        }

        // Decode surrogate pairs.
        if ((c & 0xfc00) == 0xd800)
        {
            unsigned short d = *utf16;
            if ((d & 0xfc00) == 0xdc00)
            {
                c = (c << 10) + d - 0x35fdc00;
                ++utf16;
            }
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

    return builder.truncated();
}

//------------------------------------------------------------------------------
bool convert(wchar_t* out, int max_count, const char* utf8)
{
    builder<wchar_t> builder(out, max_count);

    int ax = 0;
    int encode_length = 0;
    while (unsigned char c = *utf8++)
    {
        ax = (ax << 6) | (c & 0x7f);
        if (!encode_length)
        {
            if ((c & 0xc0) < 0xc0)
            {
                builder << ax;
                ax = 0;
                continue;
            }

            if (encode_length = !!(c & 0x20))
                encode_length += !!(c & 0x10);

            ax &= (0x1f >> encode_length);
        }
        else
            --encode_length;
    }

    return builder.truncated();
}

//------------------------------------------------------------------------------
bool convert(str_base& out, const wchar_t* utf16)
{
    return convert(out.data() + out.length(), out.size() - out.length(), utf16);
}

//------------------------------------------------------------------------------
bool convert(wstr_base& out, const char* utf8)
{
    return convert(out.data() + out.length(), out.size() - out.length(), utf8);
}
