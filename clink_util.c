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

#include "clink_pch.h"

//------------------------------------------------------------------------------
void str_cat(char* dest, const char* src, int n)
{
    int m = n - (int)strlen(dest);
    if (m > 0)
    {
        strncat(dest, src, m);
    }
    dest[n - 1] = '\0';
}

//------------------------------------------------------------------------------
void get_config_dir(char* buffer, int size)
{
    static int once = 1;
    const char* app_dir;

    buffer[0] = '\0';

    app_dir = getenv("ALLUSERSPROFILE");
    if (app_dir == NULL)
    {
        return;
    }

    str_cat(buffer, app_dir, size);
    str_cat(buffer, "/clink", size);

    if (once)
    {
        CreateDirectory(buffer, NULL);
        once = 0;
    }
}

//------------------------------------------------------------------------------
void get_dll_dir(char* buffer, int size)
{
    BOOL ok;
    HINSTANCE module;
    char* slash;

    buffer[0] = '\0';

    ok = GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (char*)get_dll_dir,
        &module
    );

    if (ok == FALSE)
    {
        return;
    }

    GetModuleFileName(module, buffer, size);
    slash = strrchr(buffer, '\\');
    if (slash != NULL)
    {
        *slash = '\0';
    }
}

//------------------------------------------------------------------------------
static void log_line_v(
    const char* function,
    int source_line,
    const char* format,
    va_list args
)
{
    FILE* file;
    char buffer[512];

    get_config_dir(buffer, sizeof_array(buffer));
    str_cat(buffer, "/clink.log", sizeof_array(buffer));

    if (format == NULL)
    {
        unlink(buffer);
        return;
    }

    file = fopen(buffer, "at");
    if (file == NULL)
    {
        return;
    }

    // Could use fprintf here, but it appears to be broken (writing to stdout
    // instead)?!
    _snprintf(buffer, sizeof_array(buffer), "%s():%d ", function, source_line);
    buffer[sizeof_array(buffer) - 1] = '\0';

    // Write out the line, tagged with function and line number.
    fputs(buffer, file);
    vfprintf(file, format, args);
    fputs("\n", file);

    fclose(file);
}

//------------------------------------------------------------------------------
void log_line(const char* function, int source_line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_line_v(function, source_line, format, args);
    va_end(args);
}

//------------------------------------------------------------------------------
void log_error(const char* function, int source_line, const char* format, ...)
{
    va_list args;
    DWORD last_error;

    last_error = GetLastError();
    va_start(args, format);

    log_line(function, source_line, "ERROR...");
    log_line_v(function, source_line, format, args);
    log_line(function, source_line, "(last_error = %d)", last_error);

    va_end(args);
}
