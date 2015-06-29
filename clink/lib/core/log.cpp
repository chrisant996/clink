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
#include "log.h"

#include <stdarg.h>

//------------------------------------------------------------------------------
file_logger::file_logger(const char* log_path)
{
    m_log_path << log_path;
}

//------------------------------------------------------------------------------
file_logger::~file_logger()
{
}

//------------------------------------------------------------------------------
void file_logger::emit(const char* function, int line, const char* fmt, va_list args)
{
    FILE* file;

    file = fopen(m_log_path.c_str(), "at");
    if (file == nullptr)
        return;

    str<24> func_name;
    func_name << function;

    DWORD pid = GetCurrentProcessId();

    str<256> buffer;
    buffer.format("%04x %-24s %4d ", pid, func_name.c_str(), line);
    fputs(buffer.c_str(), file);
    vfprintf(file, fmt, args);
    fputs("\n", file);

    fclose(file);
}

//------------------------------------------------------------------------------
void logger::info(const char* function, int line, const char* fmt, ...)
{
    logger* instance = logger::get();
    if (instance == nullptr)
        return;

    va_list args;
    va_start(args, fmt);
    instance->emit(function, line, fmt, args);
    va_end(args);
}

//------------------------------------------------------------------------------
void logger::error(const char* function, int line, const char* fmt, ...)
{
    logger* instance = logger::get();
    if (instance == nullptr)
        return;

    DWORD last_error = GetLastError();

    va_list args;
    va_start(args, fmt);

    logger::info(function, line, "*** ERROR ***");
    instance->emit(function, line, fmt, args);
    logger::info(function, line, "(last error = %d)", last_error);

    va_end(args);
}
