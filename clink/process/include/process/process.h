// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

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
struct remote_result
{
    // Char type for 'ok' allows more than just true/false; e.g. -1 to signal an
    // ignorable or silent error.
    char        ok;
    void*       result;
};

//------------------------------------------------------------------------------
class process
{
public:
    enum arch { arch_unknown, arch_x86, arch_x64, arch_arm64 };
    typedef FARPROC funcptr_t;

                                process(int pid=-1);
    int                         get_pid() const;
    bool                        get_file_name(str_base& out) const;
    arch                        get_arch() const;
    int                         get_parent_pid() const;
    remote_result               inject_module(const char* dll, process_wait_callback* callback);
    template <typename T> remote_result remote_call(funcptr_t function, T const& param);
    template <typename T1, typename T2> remote_result remote_call(funcptr_t function, T1 const& param1, T2 const& param2);
    template <typename T> remote_result remote_callex(funcptr_t function, process_wait_callback* callback, T const& param);
    template <typename T1, typename T2> remote_result remote_callex(funcptr_t function, process_wait_callback* callback, T1 const& param1, T2 const& param2);
    void                        pause();
    void                        unpause();

private:
    remote_result               remote_call_internal(funcptr_t function, process_wait_callback* callback, const void* param, int param_size);
    remote_result               remote_call_internal(funcptr_t function, process_wait_callback* callback, const void* param1, int param1_size, const void* param2, int param2_size);
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
remote_result process::remote_call(funcptr_t function, T const& param)
{
    return remote_call_internal(function, nullptr/*callback*/, &param, sizeof(param));
}

//------------------------------------------------------------------------------
template <typename T1, typename T2>
remote_result process::remote_call(funcptr_t function, T1 const& param1, T2 const& param2)
{
    return remote_call_internal(function, nullptr/*callback*/, &param1, sizeof(param1), &param2, sizeof(param2));
}

//------------------------------------------------------------------------------
template <typename T>
remote_result process::remote_callex(funcptr_t function, process_wait_callback* callback, T const& param)
{
    return remote_call_internal(function, callback, &param, sizeof(param));
}

//------------------------------------------------------------------------------
template <typename T1, typename T2>
remote_result process::remote_callex(funcptr_t function, process_wait_callback* callback, T1 const& param1, T2 const& param2)
{
    return remote_call_internal(function, callback, &param1, sizeof(param1), &param2, sizeof(param2));
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
