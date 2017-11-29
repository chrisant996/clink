// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <Windows.h>

class str_base;

//------------------------------------------------------------------------------
class process
{
public:
    enum arch { arch_unknown, arch_x86, arch_x64 };

                                process(int pid=-1);
    int                         get_pid() const;
    bool                        get_file_name(str_base& out) const;
    arch                        get_arch() const;
    int                         get_parent_pid() const;
    void*                       inject_module(const char* dll);
    template <typename T> void* remote_call(void* function, T const& param);
    void                        pause();
    void                        unpause();

private:
    void*                       remote_call(void* function, const void* param, int param_size);
    void                        pause_impl(bool suspend);
    int                         m_pid;

    struct handle
    {
        handle(HANDLE h) : m_handle(h)  {}
        ~handle()                       { CloseHandle(m_handle); }
        explicit operator bool () const { return (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE); }
        operator HANDLE () const        { return m_handle; }
        HANDLE m_handle;
    };
};

//------------------------------------------------------------------------------
inline int process::get_pid() const
{
    return m_pid;
}

//------------------------------------------------------------------------------
template <typename T>
void* process::remote_call(void* function, T const& param)
{
    return remote_call(function, &param, sizeof(param));
}

//------------------------------------------------------------------------------
inline void process::pause()
{
    pause_impl(true);
}

//------------------------------------------------------------------------------
inline void process::unpause()
{
    pause_impl(false);
}
