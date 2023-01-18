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

    if (!instance->m_deferred.empty())
        instance->emit_deferred();
    if (instance->m_can_defer)
        instance->m_can_defer = false;

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

    if (!instance->m_deferred.empty())
        instance->emit_deferred();
    if (instance->m_can_defer)
        instance->m_can_defer = false;

    va_list args;
    va_start(args, fmt);

    logger::info(function, line, "*** ERROR ***");
    instance->emit(function, line, fmt, args);
    logger::info(function, line, "(last error = %d)", last_error);

    va_end(args);
}

//------------------------------------------------------------------------------
bool logger::can_defer()
{
    logger* instance = logger::get();
    return (instance && instance->m_can_defer);
}

//------------------------------------------------------------------------------
void logger::defer_info(const char* function, int line, const char* fmt, ...)
{
    logger* instance = logger::get();
    if (instance == nullptr)
        return;

    deferred d;

    va_list args;
    va_start(args, fmt);
    d.function = function;
    d.line = line;
    d.msg.vformat(fmt, args);
    va_end(args);

    if (instance->m_can_defer)
        instance->m_deferred.emplace_back(std::move(d));
    else
        info(d.function.c_str(), d.line, "%s", d.msg.c_str());
}

//------------------------------------------------------------------------------
void logger::emit_deferred()
{
    std::vector<deferred> deferred;
    deferred.swap(m_deferred);
    for (const auto& d : deferred)
        info(d.function.c_str(), d.line, "%s", d.msg.c_str());
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
