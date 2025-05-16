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
void LOGCURSORPOS(HANDLE h);

//------------------------------------------------------------------------------
class logger
    : public singleton<logger>
{
    struct deferred
    {
        str_moveable function;
        int32 line;
        str_moveable msg;
    };

    friend class logging_group;

public:
    virtual         ~logger();
    static void     info(const char* function, int32 line, const char* fmt, ...);
    static void     error(const char* function, int32 line, const char* fmt, ...);

    static bool     can_defer();
    static void     defer_info(const char* function, int32 line, const char* fmt, ...);

private:
    void            emit(const char* function, int32 line, const char* fmt, va_list args);
    virtual void    emit_impl(const char* function, int32 line, const char* msg) = 0;

    void            emit_deferred();
    size_t          begin_group();
    void            end_group(size_t rollback_index, bool discard);

private:
    int32           m_grouping = 0;
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
    virtual void    emit_impl(const char* function, int32 line, const char* msg) override;

    static const char* get_path() { return s_this ? s_this->m_log_path.c_str() : nullptr; }

private:
    str<256>        m_log_path;

    static const file_logger* s_this;
};

//------------------------------------------------------------------------------
// WARNING:  Use logging_group in a strictly nested manner.  If an inner group
// outlasts an outer group, the results are undefined!
class logging_group
{
public:
                    // WARNING:  logging_group is not threadsafe!  Be sure to
                    // add thread synchronization before adding any usage on a
                    // background thread after initialise_clink() completes.
                    logging_group(bool verbose=false);
                    ~logging_group();

    void            close(bool discard);
    void            discard() { close(true); }
    bool            is_verbose() const { return m_verbose; }

private:
    bool            m_grouping = false;
    bool            m_verbose = true;
    size_t          m_rollback_index = 0;
};
