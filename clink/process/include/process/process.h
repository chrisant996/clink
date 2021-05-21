// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <Windows.h>

class str_base;

//------------------------------------------------------------------------------
struct process_wait_callback
{
    // Return the timeout interval.
    virtual DWORD get_timeout() { return 1000; }

    // Return true to stop waiting.
    virtual bool on_waited(DWORD tick_begin, DWORD wait_result) = 0;
};

//------------------------------------------------------------------------------
class process
{
public:
    enum arch { arch_unknown, arch_x86, arch_x64 };
    typedef FARPROC funcptr_t;

                                process(int pid=-1);
    int                         get_pid() const;
    bool                        get_file_name(str_base& out) const;
    arch                        get_arch() const;
    int                         get_parent_pid() const;
    void*                       inject_module(const char* dll, process_wait_callback* callback);
    template <typename T> void* remote_call(funcptr_t function, T const& param);
    template <typename T1, typename T2> void* remote_call(funcptr_t function, T1 const& param1, T2 const& param2);
    void                        pause();
    void                        unpause();

private:
    void*                       remote_call_internal(funcptr_t function, process_wait_callback* callback, const void* param, int param_size);
    void*                       remote_call_internal(funcptr_t function, process_wait_callback* callback, const void* param1, int param1_size, const void* param2, int param2_size);
    DWORD                       wait(process_wait_callback* callback, HANDLE remote_thread);
    void                        pause(bool suspend);
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
void* process::remote_call(funcptr_t function, T const& param)
{
    return remote_call_internal(function, nullptr/*callback*/, &param, sizeof(param));
}

//------------------------------------------------------------------------------
template <typename T1, typename T2>
void* process::remote_call(funcptr_t function, T1 const& param1, T2 const& param2)
{
    return remote_call_internal(function, nullptr/*callback*/, &param1, sizeof(param1), &param2, sizeof(param2));
}

//------------------------------------------------------------------------------
inline void process::pause()
{
    pause(true);
}

//------------------------------------------------------------------------------
inline void process::unpause()
{
    pause(false);
}
