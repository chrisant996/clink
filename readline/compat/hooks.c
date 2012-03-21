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

#include <Windows.h>

#include <stdio.h>
#include <wchar.h>
#include <sys/stat.h>

#include "hooks.h"

//------------------------------------------------------------------------------
#define sizeof_array(x) (sizeof(x) / sizeof(x[0]))

//------------------------------------------------------------------------------
int hooked_fwrite(const void* data, int size, int count, void* unused)
{
    wchar_t buf[2048];
    size_t characters;
    DWORD written;

    size *= count;
    
    characters = MultiByteToWideChar(
        CP_UTF8, 0,
        (const char*)data, size,
        buf, sizeof_array(buf)
    );

    characters = characters ? characters : sizeof_array(buf) - 1;
    buf[characters] = L'\0';

    WriteConsoleW(
        GetStdHandle(STD_OUTPUT_HANDLE),
        buf,
        (DWORD)wcslen(buf),
        &written,
        NULL
    );

    return size;
}

//------------------------------------------------------------------------------
void hooked_fprintf(const void* unused, const char* format, ...)
{
    char buffer[2048];
    va_list v;

    va_start(v, format);
    vsnprintf(buffer, sizeof_array(buffer), format, v);
    va_end(v);

    buffer[sizeof_array(buffer) - 1] = '\0';
    hooked_fwrite(buffer, (int)strlen(buffer), 1, NULL);
}

//------------------------------------------------------------------------------
int hooked_putc(int c, void* unused)
{
    char buf[2] = { (char)c, '\0' };
    hooked_fwrite(buf, 1, 1, NULL);
    return 1;
}

//------------------------------------------------------------------------------
size_t hooked_mbrtowc(wchar_t* out, const char* in, size_t size, mbstate_t* state)
{
    MultiByteToWideChar(CP_UTF8, 0, in, 5, out, 1);
    return (*out > 0) + (*out > 0x7f) + (*out > 0x7ff);
}

//------------------------------------------------------------------------------
size_t hooked_mbrlen(const char* in, size_t size, mbstate_t* state)
{
    wchar_t t;
    return hooked_mbrtowc(&t, in, size, NULL);
}

//------------------------------------------------------------------------------
int hooked_stat(const char* path, struct hooked_stat* out)
{
    wchar_t buf[2048];
    size_t characters;
#if defined(_MSC_VER)
    struct _stat64i32 s;
#else
    struct _stat s;
#endif
    int ret;

    // UTF8 to wchars.
    characters = MultiByteToWideChar(
        CP_UTF8, 0,
        path, -1,
        buf, sizeof_array(buf)
    );

    characters = characters ? characters : sizeof_array(buf) - 1;
    buf[characters] = L'\0';

    // Get stats, convert to magic struct hack thing.
    ret = _wstat(buf, &s);
    out->st_size = s.st_size;
    out->st_mode = s.st_mode;

    return ret;

}

//------------------------------------------------------------------------------
int hooked_fstat(int fid, struct hooked_stat* out)
{
    int ret;
    struct stat s;

    ret = fstat(fid, &s);
    out->st_size = s.st_size;
    out->st_mode = s.st_mode;

    return ret;

}
