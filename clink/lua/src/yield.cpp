// Copyright (c) 2021-2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "yield.h"
#include "lua_state.h"

#include <core/os.h>

#include <process.h>
#include <assert.h>

//------------------------------------------------------------------------------
static HANDLE s_wake_event = nullptr;

//------------------------------------------------------------------------------
void set_yield_wake_event(HANDLE event)
{
    // Borrow a ref from the caller.
    s_wake_event = event;
}



//------------------------------------------------------------------------------
yield_thread::yield_thread()
{
}

//------------------------------------------------------------------------------
yield_thread::~yield_thread()
{
    if (m_thread_handle)
    {
        cancel();
        CloseHandle(m_thread_handle);
    }
    if (m_ready_event)
        CloseHandle(m_ready_event);
}

//------------------------------------------------------------------------------
bool yield_thread::createthread()
{
    assert(!m_suspended);
    assert(!m_thread_handle);
    assert(!m_cancelled);
    assert(!m_ready_event);
    os::get_current_dir(m_cwd);
    if (!s_wake_event)
        return false;
    m_ready_event = CreateEvent(nullptr, true, false, nullptr);
    if (!m_ready_event)
        return false;
    m_thread_handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &threadproc, this, CREATE_SUSPENDED, nullptr));
    if (!m_thread_handle)
        return false;
    m_holder = shared_from_this(); // Now threadproc holds a strong ref.
    m_suspended = true;
    return true;
}

//------------------------------------------------------------------------------
void yield_thread::go()
{
    assert(m_suspended);
    if (m_thread_handle)
    {
        m_suspended = false;
        ResumeThread(m_thread_handle);
    }
}

//------------------------------------------------------------------------------
void yield_thread::cancel()
{
    m_cancelled = true;
    if (m_suspended) // Can only be true when there's no concurrency.
        ResumeThread(m_thread_handle);
}

//------------------------------------------------------------------------------
bool yield_thread::is_ready()
{
    HANDLE event = get_ready_event();
    if (!event)
        return false;
    return WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

//------------------------------------------------------------------------------
HANDLE yield_thread::get_ready_event()
{
    return m_ready_event;
}

//------------------------------------------------------------------------------
void yield_thread::set_need_completion()
{
    // The base class implementation should never be reached.
    assert(false);
}

//------------------------------------------------------------------------------
void yield_thread::wait(uint32 timeout)
{
    HANDLE event = get_ready_event();
    if (event)
        WaitForSingleObject(event, timeout);
}

//------------------------------------------------------------------------------
bool yield_thread::is_canceled() const
{
    return !!m_cancelled;
}

//------------------------------------------------------------------------------
const char* yield_thread::get_cwd() const
{
    return m_cwd.c_str();
}

//------------------------------------------------------------------------------
unsigned __stdcall yield_thread::threadproc(void *arg)
{
    yield_thread *_this = static_cast<yield_thread *>(arg);

    // Do the work defined by the subclass.
    _this->do_work();

    // Signal completion events.
    SetEvent(_this->m_ready_event);
    SetEvent(s_wake_event);

    // Give subclass a chance to do completion processing.
    if (_this->do_completion())
        SetEvent(s_wake_event);

    // Release threadproc's strong ref.
    _this->m_holder = nullptr;

    _endthreadex(0);
    return 0;
}



//------------------------------------------------------------------------------
#define LUA_YIELDGUARD "clink_yield_guard"

//------------------------------------------------------------------------------
luaL_YieldGuard* luaL_YieldGuard::make_new(lua_State* state)
{
#ifdef DEBUG
    int32 oldtop = lua_gettop(state);
#endif

    luaL_YieldGuard* yg = (luaL_YieldGuard*)lua_newuserdata(state, sizeof(luaL_YieldGuard));
    new (yg) luaL_YieldGuard();

    static const luaL_Reg yglib[] =
    {
        {"ready", ready},
        {"command", command},
        {"set_need_completion", set_need_completion},
        {"results", results},
        {"wait", wait},
        {"__gc", __gc},
        {"__tostring", __tostring},
        {nullptr, nullptr}
    };

    if (luaL_newmetatable(state, LUA_YIELDGUARD))
    {
        lua_pushvalue(state, -1);           // push metatable
        lua_setfield(state, -2, "__index"); // metatable.__index = metatable
        luaL_setfuncs(state, yglib, 0);     // add methods to new metatable
    }
    lua_setmetatable(state, -2);

#ifdef DEBUG
    int32 newtop = lua_gettop(state);
    assert(oldtop - newtop == -1);
    luaL_YieldGuard* test = (luaL_YieldGuard*)luaL_checkudata(state, -1, LUA_YIELDGUARD);
    assert(test == yg);
#endif

    return yg;
}

//------------------------------------------------------------------------------
void luaL_YieldGuard::init(std::shared_ptr<yield_thread> thread, const char* command)
{
    m_thread = thread;
    m_command = command;
}

//------------------------------------------------------------------------------
const char* luaL_YieldGuard::get_command() const
{
    return this ? m_command.c_str() : nullptr;
}

//------------------------------------------------------------------------------
int32 luaL_YieldGuard::ready(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    lua_pushboolean(state, yg && yg->m_thread && yg->m_thread->is_ready());
    return 1;
}

//------------------------------------------------------------------------------
int32 luaL_YieldGuard::command(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    lua_pushstring(state, yg->get_command());
    return 1;
}

//------------------------------------------------------------------------------
int32 luaL_YieldGuard::set_need_completion(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    if (yg && yg->m_thread)
        yg->m_thread->set_need_completion();
    return 0;
}

//------------------------------------------------------------------------------
int32 luaL_YieldGuard::results(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    if (yg && yg->m_thread)
        return yg->m_thread->results(state);
    return 0;
}

//------------------------------------------------------------------------------
int32 luaL_YieldGuard::wait(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    if (yg && yg->m_thread)
    {
        int32 isnum;
        const double sec = lua_tonumberx(state, 2, &isnum);
        const uint32 timeout = (!isnum ? INFINITE :
                                (sec > 0) ? unsigned(sec * 1000) :
                                0);
        yg->m_thread->wait(timeout);
    }
    return 0;
}

//------------------------------------------------------------------------------
int32 luaL_YieldGuard::__gc(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    if (yg)
        yg->~luaL_YieldGuard();
    return 0;
}

//------------------------------------------------------------------------------
int32 luaL_YieldGuard::__tostring(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    lua_pushfstring(state, "yieldguard (%p)", yg ? yg->m_thread.get() : nullptr);
    return 1;
}
