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
    int     get_pid() const;
    bool    get_file_name(str_base& out) const;
    arch    get_arch() const;
    int     get_parent_pid() const;
    bool    inject_module(const char* dll);
    int     remote_call(void* function, void* param);
    void    pause();
    void    unpause();

private:
    void    pause_impl(bool suspend);
    int     m_pid;

    struct Handle
    {
        Handle(HANDLE h) : m_handle(h)  {}
        ~Handle()                       { CloseHandle(m_handle); }
        operator bool () const          { return (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE); }
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
inline void process::pause()
{
    pause_impl(true);
}

//------------------------------------------------------------------------------
inline void process::unpause()
{
    pause_impl(false);
}
