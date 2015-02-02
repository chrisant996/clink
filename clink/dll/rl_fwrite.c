/* Copyright (c) 2012 Martin Ridgers
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
#include "shared/util.h"

//------------------------------------------------------------------------------
const wchar_t*      find_next_ansi_code_w(const wchar_t*, int*);
int                 get_clink_setting_int(const char*);
int                 parse_ansi_code_w(const wchar_t*, int*, int);

//------------------------------------------------------------------------------
static int ansi_to_attr(int colour)
{
    static const int map[] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    return map[colour & 7];
}

//------------------------------------------------------------------------------
static int fwrite_ansi_code(const wchar_t* code, int current, int defaults)
{
    int i, n;
    int params[32];
    HANDLE handle;
    int attr;

    i = parse_ansi_code_w(code, params, sizeof_array(params));
    if (i != 'm')
        return current;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    attr = current;

    // Count the number of parameters the code has.
    n = 0;
    while (n < sizeof_array(params) && params[n] >= 0)
    {
        ++n;
    }

    // Process each code that is supported.
    for (i = 0; i < n; ++i)
    {
        int param = params[i];

        if (param == 0) // reset
        {
            attr = defaults;
        }
        else if (param == 1) // fg intensity (bright)
        {
            attr |= 0x08;
        }
        else if (param == 2 || param == 22) // fg intensity (normal)
        {
            attr &= ~0x08;
        }
        else if (param == 4) // bg intensity (bright)
        {
            attr |= 0x80;
        }
        else if (param == 24) // bg intensity (normal)
        {
            attr &= ~0x80;
        }
        else if ((unsigned int)param - 30 < 8) // fg colour
        {
            attr = (attr & 0xf8) | ansi_to_attr(param - 30);
        }
        else if (param == 39) // default fg colour
        {
            attr = (attr & 0xf8) | (defaults & 0x07);
        }
        else if ((unsigned int)param - 40 < 8) // bg colour
        {
            attr = (attr & 0x8f) | (ansi_to_attr(param - 40) << 4);
        }
        else if (param == 49) // default bg colour
        {
            attr = (attr & 0x8f) | (defaults & 0x70);
        }
        else if (param == 38 || param == 48) // extended colour (skipped)
        {
            // format = param;5;[0-255] or param;2;r;g;b
            ++i;
            if (i >= n)
                break;

            switch (params[i])
            {
            case 2: i += 3; break;
            case 5: i += 1; break;
            }

            continue;
        }
    }

    SetConsoleTextAttribute(handle, attr);
    return attr;
}

//------------------------------------------------------------------------------
void fwrite_hook(wchar_t* str)
{
    const wchar_t* next;
    HANDLE handle;
    DWORD written;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int attr_def;
    int attr_cur;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);

    attr_def = csbi.wAttributes;
    attr_cur = attr_def;
    next = str;
    while (*next)
    {
        int ansi_size;
        const wchar_t* ansi_code;

        ansi_code = find_next_ansi_code_w(next, &ansi_size);

        // Dispatch console write
        WriteConsoleW(handle, next, ansi_code - next, &written, NULL);

        // Process ansi code.
        if (*ansi_code)
        {
            attr_cur = fwrite_ansi_code(ansi_code, attr_cur, attr_def);
        }

        next = ansi_code + ansi_size;
    }

    SetConsoleTextAttribute(handle, attr_def);
}

//------------------------------------------------------------------------------
void initialise_fwrite()
{
    extern void (*g_alt_fwrite_hook)(wchar_t*);

    // The test framework may have got there before us so do not overwrite it.
    if (g_alt_fwrite_hook != NULL)
        return;

    // Check for the presence of known third party tools that also provide ANSI
    // escape code support.
    {
        int i;
        const char* dll_names[] = {
            "conemuhk.dll",
            "conemuhk64.dll",
            "ansi.dll",
            "ansi32.dll",
            "ansi64.dll",
        };

        for (i = 0; i < sizeof_array(dll_names); ++i)
        {
            const char* dll_name = dll_names[i];
            if (GetModuleHandle(dll_name) != NULL)
            {
                LOG_INFO("Disabling ANSI support. Found '%s'", dll_name);
                return;
            }
        }
    }

    // Give the user the option to disable ANSI support.
    if (get_clink_setting_int("ansi_code_support") == 0)
        return;

    g_alt_fwrite_hook = fwrite_hook;
}
