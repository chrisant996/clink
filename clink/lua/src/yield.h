// Copyright (c) 2021-2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

#include <memory>

struct lua_State;

//------------------------------------------------------------------------------
struct yield_thread : public std::enable_shared_from_this<yield_thread>
{
                    yield_thread();
    virtual         ~yield_thread();

    bool            createthread();

    void            go();
    void            cancel();

    bool            is_ready();
    HANDLE          get_ready_event();

    void            wait(unsigned int timeout);

    virtual int     results(lua_State* state) = 0;

protected:
    bool            is_canceled() const;
    const char*     get_cwd() const;

private:
    virtual void    do_work() = 0;

    static unsigned __stdcall threadproc(void* arg);

    HANDLE m_thread_handle = 0;
    HANDLE m_ready_event = 0;
    HANDLE m_wake_event = 0;
    str_moveable m_cwd;
    bool m_suspended = false;

    volatile long m_cancelled = false;
    volatile long m_ready = false;

    std::shared_ptr<yield_thread> m_holder;
};

//------------------------------------------------------------------------------
struct luaL_YieldGuard
{
    static luaL_YieldGuard* make_new(lua_State* state);

    void init(std::shared_ptr<yield_thread> thread, const char* command);

protected:
    const char* get_command() const;

private:
    static int ready(lua_State* state);
    static int command(lua_State* state);
    static int results(lua_State* state);
    static int wait(lua_State* state);
    static int __gc(lua_State* state);
    static int __tostring(lua_State* state);

    std::shared_ptr<yield_thread> m_thread;
    str_moveable m_command;
};
