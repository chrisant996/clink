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
    virtual HANDLE  get_ready_event();
    virtual void    set_need_completion();

    void            wait(uint32 timeout);

    virtual int32   results(lua_State* state) = 0;

protected:
    bool            is_canceled() const;
    const char*     get_cwd() const;

private:
    virtual void    do_work() = 0;
    virtual bool    do_completion() { return false; }

    static unsigned __stdcall threadproc(void* arg);

    HANDLE m_thread_handle = 0;
    HANDLE m_ready_event = 0;
    str_moveable m_cwd;
    bool m_suspended = false;

    volatile long m_cancelled = false;

    std::shared_ptr<yield_thread> m_holder;
};

//------------------------------------------------------------------------------
struct luaL_YieldGuard
{
    static luaL_YieldGuard* make_new(lua_State* state);

    void init(std::shared_ptr<yield_thread> thread, const char* command);

protected:
    ~luaL_YieldGuard() = default;
    const char* get_command() const;

private:
    static int32 ready(lua_State* state);
    static int32 command(lua_State* state);
    static int32 set_need_completion(lua_State* state);
    static int32 results(lua_State* state);
    static int32 wait(lua_State* state);
    static int32 __gc(lua_State* state);
    static int32 __tostring(lua_State* state);

    std::shared_ptr<yield_thread> m_thread;
    str_moveable m_command;
};
