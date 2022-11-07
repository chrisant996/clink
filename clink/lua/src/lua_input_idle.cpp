// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_input_idle.h"
#include "lua_state.h"
#include "async_lua_task.h"

#include <core/base.h>
#include <lib/reclassify.h>

#include <assert.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
extern void host_filter_prompt();
extern void host_invalidate_matches();
extern void set_yield_wake_event(HANDLE event);
static lua_input_idle* s_idle = nullptr;

//------------------------------------------------------------------------------
void kick_idle()
{
    if (s_idle)
        s_idle->kick();
}

//------------------------------------------------------------------------------
bool lua_input_idle::s_signaled_delayed_init = false;
bool lua_input_idle::s_signaled_reclassify = false;
static bool s_refilter_after_terminal_resize = false;
static DWORD s_terminal_resized = 0;    // Tick count of most recent resize.
static HANDLE s_wake_event = 0;

// After the terminal is resized, wait for this many milliseconds before
// automatically rerunning the prompt filters.
const DWORD c_terminal_resize_refilter_delay = 500;

//------------------------------------------------------------------------------
lua_input_idle::lua_input_idle(lua_state& state)
: m_state(state)
{
    assert(!s_idle);
    s_idle = this;
    if (!s_wake_event)
    {
        s_wake_event = CreateEvent(nullptr, false, false, nullptr);
        set_yield_wake_event(s_wake_event);
    }
}

//------------------------------------------------------------------------------
lua_input_idle::~lua_input_idle()
{
    s_idle = nullptr;
}

//------------------------------------------------------------------------------
void lua_input_idle::reset()
{
    s_signaled_delayed_init = false;
    s_signaled_reclassify = false;

    m_enabled = true;
    m_iterations = 0;
}

//------------------------------------------------------------------------------
bool lua_input_idle::is_enabled()
{
    if (!m_enabled)
        return false;

    if (!has_coroutines())
        m_enabled = false;

    return m_enabled;
}

//------------------------------------------------------------------------------
unsigned lua_input_idle::get_timeout()
{
    // When terminal resize handling is active, it controls the timeout.
    // Coroutines are not resumed while the terminal is being resized.
    if (s_terminal_resized)
    {
        const DWORD timeout = c_terminal_resize_refilter_delay - (GetTickCount() - s_terminal_resized);
        return (timeout < c_terminal_resize_refilter_delay) ? timeout : 0;
    }

    m_iterations++;

    if (!is_enabled())
        return INFINITE;

    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to check for coroutines.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_wait_duration");
    lua_rawget(state, -2);

    if (m_state.pcall(state, 0, 1) != 0)
        return INFINITE;

    int isnum;
    double sec = lua_tonumberx(state, -1, &isnum);
    if (!isnum)
        return INFINITE;

    return (sec > 0) ? unsigned(sec * 1000) : 0;
}

//------------------------------------------------------------------------------
void* lua_input_idle::get_waitevent()
{
    return s_wake_event;
}

//------------------------------------------------------------------------------
void lua_input_idle::on_idle()
{
    // Don't resume coroutines while the terminal resize timeout is in effect,
    // as that would bypass the resume frequency logic.
    if (s_terminal_resized)
    {
        // If it's been more than 0.5 seconds since the terminal was last
        // resized, then refilter the prompt.  Note that if automatic refilter
        // isn't enabled, then s_terminal_resized won't be set in the first
        // place, and this won't be reached.
        if (GetTickCount() - s_terminal_resized >= c_terminal_resize_refilter_delay)
        {
            s_terminal_resized = 0;
            host_filter_prompt();
        }
    }
    else
    {
        // An example how this can be reached with !m_enabled is when
        // io.popenyield() wakes idle after getting the process exit code, but
        // it can't know whether any Lua code has/will yield to wait for the
        // exit code.  If Lua code hasn't finished or is yielding to wait,
        // then m_enabled will still be true.  But if the corresponding Lua
        // coroutine finished without ever yielding to wait for the exit code
        // then m_enabled may be false, if that was the last coroutine.  In
        // that case, there are no coroutines and so short circuiting is an
        // appropriate optimization here.
        if (m_enabled)
            resume_coroutines();
    }

    if (s_signaled_delayed_init)
    {
        s_signaled_delayed_init = false;
        host_invalidate_matches();
    }

    if (s_signaled_reclassify)
    {
        s_signaled_reclassify = false;
        host_reclassify(reclassify_reason::force);
    }
}

//------------------------------------------------------------------------------
void lua_input_idle::on_task_manager()
{
    task_manager_on_idle(m_state);
}

//------------------------------------------------------------------------------
void lua_input_idle::kick()
{
    if (!m_enabled && has_coroutines())
    {
        m_enabled = true;
    }
}

//------------------------------------------------------------------------------
void lua_input_idle::signal_delayed_init()
{
    s_signaled_delayed_init = true;
}

//------------------------------------------------------------------------------
void lua_input_idle::signal_reclassify()
{
    s_signaled_reclassify = true;
}

//------------------------------------------------------------------------------
void set_refilter_after_resize(bool refilter)
{
    s_refilter_after_terminal_resize = refilter;
    if (!refilter)
        s_terminal_resized = 0;
}

//------------------------------------------------------------------------------
void signal_terminal_resized()
{
    if (s_refilter_after_terminal_resize)
        s_terminal_resized = GetTickCount();
    else if (s_terminal_resized)
        s_terminal_resized = 0;
}

//------------------------------------------------------------------------------
bool lua_input_idle::has_coroutines()
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to check for coroutines.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_has_coroutines");
    lua_rawget(state, -2);

    if (m_state.pcall(state, 0, 1) != 0)
        return false;

    bool has = lua_toboolean(state, -1);
    return has;
}

//------------------------------------------------------------------------------
void lua_input_idle::resume_coroutines()
{
    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to check for coroutines.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_resume_coroutines");
    lua_rawget(state, -2);

    m_state.pcall(state, 0, 0);
}
