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

#include <core/str.h>

//------------------------------------------------------------------------------
static int g_disable_log = 0;

//------------------------------------------------------------------------------
static void log_line_v(
    const char* function,
    int source_line,
    const char* format,
    va_list args
)
{
    FILE* file;
    DWORD pid = GetCurrentProcessId();

    str<256> buffer;
    get_log_dir(buffer);
    buffer << "/clink.log";

    if (format == nullptr)
    {
        unlink(buffer.c_str());
        return;
    }

    file = fopen(buffer.c_str(), "at");
    if (file == nullptr)
        return;

    // Write out the line, tagged with function and line number.
    buffer.format("%5d %-25s %4d ", pid, function, source_line);
    fputs(buffer.c_str(), file);
    vfprintf(file, format, args);
    fputs("\n", file);

    fclose(file);
}

//------------------------------------------------------------------------------
void log_line(const char* function, int source_line, const char* format, ...)
{
    if (g_disable_log)
        return;

    va_list args;
    va_start(args, format);
    log_line_v(function, source_line, format, args);
    va_end(args);
}

//------------------------------------------------------------------------------
void log_error(const char* function, int source_line, const char* format, ...)
{
    if (g_disable_log)
        return;

    va_list args;
    DWORD last_error;

    last_error = GetLastError();
    va_start(args, format);

    log_line(function, source_line, "ERROR...");
    log_line_v(function, source_line, format, args);
    log_line(function, source_line, "(last_error = %d)", last_error);

    va_end(args);
}

//------------------------------------------------------------------------------
void disable_log()
{
    g_disable_log = 1;
}
