/* Copyright (c) 2013 Martin Ridgers
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
#include "util.h"

//------------------------------------------------------------------------------
void str_cat(char* dest, const char* src, int max)
{
    int m = max - (int)strlen(dest) - 1;
    if (m > 0)
    {
        strncat(dest, src, m);
    }
}

//------------------------------------------------------------------------------
void str_cat_n(char* dest, const char* src, int max, int n)
{
    int m;

    m = max - (int)strlen(dest) - 1;
    m = (n < m) ? n : m;
    if (m > 0)
    {
        strncat(dest, src, m);
    }
}

//------------------------------------------------------------------------------
void str_cpy(char* dest, const char* src, int max)
{
    dest[0] = '\0';
    str_cat(dest, src, max);
}

//------------------------------------------------------------------------------
int hash_string(const char* str)
{
    int hash = 0;
    int c;

    while (c = *str++)
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

//------------------------------------------------------------------------------
void wrapped_write(FILE* out, const char* prefix, const char* str, int width)
{
    int i;
    char buffer[256];

    width = (sizeof_array(buffer) > width) ? width : sizeof_array(buffer);

    i = (int)strlen(str);
    while (i > 0)
    {
        const char* end;

        i -= width;
        end = str + width;

        if (i > 0)
        {
            // Work out the wrap point be searching backwards from the end of the
            // printed with looking for whitespace.
            while (!isspace(*end) && end > str)
            {
                --end;
            }
            ++end;
        }

        str_cpy(buffer, str, (int)(intptr_t)(end - str));
        fprintf(out, "%s%s\n", prefix, buffer);

        i += width - (int)(intptr_t)(end - str);
        str = end;
    }
}

// vim: expandtab
