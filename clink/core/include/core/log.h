// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"
#include "singleton.h"

//------------------------------------------------------------------------------
#define LOG(...)    logger::info(__FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ERR(...)    logger::error(__FUNCTION__, __LINE__, ##__VA_ARGS__)

//------------------------------------------------------------------------------
class logger
    : public singleton<logger>
{
public:
    virtual         ~logger();
    static void     info(const char* function, int line, const char* fmt, ...);
    static void     error(const char* function, int line, const char* fmt, ...);

protected:
    virtual void    emit(const char* function, int line, const char* fmt, va_list args) = 0;
};

//------------------------------------------------------------------------------
class file_logger
    : public logger
{
public:
                    file_logger(const char* log_path);
    virtual void    emit(const char* function, int line, const char* fmt, va_list args) override;

private:
    str<256>        m_log_path;
};
