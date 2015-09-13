// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "log.h"

#include <stdarg.h>

//------------------------------------------------------------------------------
logger::~logger()
{
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



//------------------------------------------------------------------------------
file_logger::file_logger(const char* log_path)
{
    m_log_path << log_path;
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
