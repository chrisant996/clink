// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "log.h"
#include "os.h"

#include <stdarg.h>

//------------------------------------------------------------------------------
void LOGCURSORPOS()
{
    LOGCURSORPOS(GetStdHandle(STD_OUTPUT_HANDLE));
}

//------------------------------------------------------------------------------
void LOGCURSORPOS(HANDLE h)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi))
        LOG("CURSORPOS %d,%d", csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y);
}

//------------------------------------------------------------------------------
logger::~logger()
{
}

//------------------------------------------------------------------------------
void logger::info(const char* function, int32 line, const char* fmt, ...)
{
    logger* instance = logger::get();
    if (instance == nullptr)
        return;

    instance->emit_deferred();

    va_list args;
    va_start(args, fmt);
    instance->emit(function, line, fmt, args);
    va_end(args);
}

//------------------------------------------------------------------------------
void logger::error(const char* function, int32 line, const char* fmt, ...)
{
    logger* instance = logger::get();
    if (instance == nullptr)
        return;

    DWORD last_error = GetLastError();

    instance->emit_deferred();

    va_list args;
    va_start(args, fmt);

    if ((last_error >= 0) || (last_error >> 16 == 0xffff))
        logger::info(function, line, "*** ERROR %d ***", last_error);
    else
        logger::info(function, line, "*** ERROR 0x%8.8X ***", last_error);
    instance->emit(function, line, fmt, args);

    str_moveable err;
    if (os::format_error_message(last_error, err))
        logger::info(function, line, "(%s)", err.c_str());

    va_end(args);
}

//------------------------------------------------------------------------------
bool logger::can_defer()
{
    logger* instance = logger::get();
    return (instance && instance->m_can_defer);
}

//------------------------------------------------------------------------------
void logger::defer_info(const char* function, int32 line, const char* fmt, ...)
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
void logger::emit(const char* function, int32 line, const char* fmt, va_list args)
{
    str_moveable msg;
    msg.vformat(fmt, args);

    if (m_grouping <= 0)
    {
        emit_impl(function, line, msg.c_str());
    }
    else
    {
        deferred d;
        d.function = function;
        d.line = line;
        d.msg = std::move(msg);
        m_deferred.emplace_back(std::move(d));
    }
}

//------------------------------------------------------------------------------
void logger::emit_deferred()
{
    if (m_grouping <= 0 && !m_deferred.empty())
    {
        std::vector<deferred> deferred;
        deferred.swap(m_deferred);
        for (const auto& d : deferred)
            emit_impl(d.function.c_str(), d.line, d.msg.c_str());
    }

    if (m_can_defer)
        m_can_defer = false;
}

//------------------------------------------------------------------------------
size_t logger::begin_group()
{
    // It's a usage error if begin_group() is called before any calls to
    // info() or error().
    assert(!can_defer());
    assert(m_deferred.empty());
    ++m_grouping;
    return m_deferred.size();
}

//------------------------------------------------------------------------------
void logger::end_group(size_t rollback_index, bool discard)
{
    assert(m_grouping > 0);
    assert(rollback_index <= m_deferred.size());
    --m_grouping;
    if (rollback_index <= m_deferred.size())
    {
        if (!discard && m_grouping <= 0)
        {
            for (size_t i = rollback_index; i < m_deferred.size(); ++i)
            {
                const auto& d = m_deferred[i];
                emit_impl(d.function.c_str(), d.line, d.msg.c_str());
            }
            discard = true;
        }
        if (discard)
            m_deferred.resize(rollback_index);
    }
}



//------------------------------------------------------------------------------
const file_logger* file_logger::s_this = nullptr;

//------------------------------------------------------------------------------
file_logger::file_logger(const char* log_path)
{
    m_log_path << log_path;
    s_this = this;
}

//------------------------------------------------------------------------------
file_logger::~file_logger()
{
    s_this = nullptr;
}

//------------------------------------------------------------------------------
void file_logger::emit_impl(const char* function, int32 line, const char* msg)
{
    FILE* file;

    file = fopen(m_log_path.c_str(), "at");
    if (file == nullptr)
        return;

    str<24> func_name;
    func_name << function;

    DWORD pid = GetCurrentProcessId();

    str<256> buffer;
    buffer.format("%04x %-24s %4d %s\n", pid, func_name.c_str(), line, msg);
    fputs(buffer.c_str(), file);

    fclose(file);
}



//------------------------------------------------------------------------------
logging_group::logging_group(bool verbose)
{
    m_verbose = verbose;
    if (verbose)
        return;

    logger* instance = logger::get();
    if (instance == nullptr)
        return;

    m_grouping = true;
    m_rollback_index = instance->begin_group();
}

//------------------------------------------------------------------------------
logging_group::~logging_group()
{
    close(false/*discard*/);
}

//------------------------------------------------------------------------------
void logging_group::close(bool discard)
{
    if (!m_grouping)
        return;

    m_grouping = false;

    logger* instance = logger::get();
    assert(instance);
    if (instance == nullptr)
        return;

    instance->end_group(m_rollback_index, discard);
}
