// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"
#include "singleton.h"

#include <vector>

//------------------------------------------------------------------------------
#define LOG(...)    logger::info(__FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ERR(...)    logger::error(__FUNCTION__, __LINE__, ##__VA_ARGS__)
#define DEFER_LOG(...) logger::defer_info(__FUNCTION__, __LINE__, ##__VA_ARGS__)

//------------------------------------------------------------------------------
class logger
    : public singleton<logger>
{
    struct deferred
    {
        str_moveable function;
        int line;
        str_moveable msg;
    };

public:
    virtual         ~logger();
    static void     info(const char* function, int line, const char* fmt, ...);
    static void     error(const char* function, int line, const char* fmt, ...);

    static bool     can_defer();
    static void     defer_info(const char* function, int line, const char* fmt, ...);

protected:
    virtual void    emit(const char* function, int line, const char* fmt, va_list args) = 0;

    void            emit_deferred();

private:
    std::vector<deferred> m_deferred;
    bool            m_can_defer = true;
};

//------------------------------------------------------------------------------
class file_logger
    : public logger
{
public:
                    file_logger(const char* log_path);
                    ~file_logger();
    virtual void    emit(const char* function, int line, const char* fmt, va_list args) override;

    static const char* get_path() { return s_this ? s_this->m_log_path.c_str() : nullptr; }

private:
    str<256>        m_log_path;

    static const file_logger* s_this;
};
