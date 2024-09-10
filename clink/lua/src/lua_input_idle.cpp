// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_input_idle.h"
#include "lua_state.h"
#include "lua_task_manager.h"
#include "async_lua_task.h"

#include <core/base.h>
#include <lib/reclassify.h>
#include <lib/line_editor_integration.h>

#include <assert.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
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

// After the terminal is resized, wait for this many milliseconds before
// automatically rerunning the prompt filters.
const DWORD c_terminal_resize_refilter_delay = 500;

//------------------------------------------------------------------------------
lua_input_idle::lua_input_idle(lua_state& state)
: m_state(state)
{
    assert(!s_idle);
    s_idle = this;
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
uint32 lua_input_idle::get_timeout()
{
    // When terminal resize handling is active, it controls the timeout.
    // While the terminal is being resized, coroutines are not resumed and
    // input hinters are not run.
    if (s_terminal_resized)
    {
        const DWORD timeout = c_terminal_resize_refilter_delay - (GetTickCount() - s_terminal_resized);
        return (timeout < c_terminal_resize_refilter_delay) ? timeout : 0;
    }

    DWORD timeout = INFINITE;

    {
        const DWORD t = host_get_input_hint_timeout();
        timeout = min(timeout, t);
    }

    if (is_enabled())
    {
        m_iterations++;

        lua_State* state = m_state.get_state();
        save_stack_top ss(state);

        // Call to Lua to check for coroutines.
        lua_getglobal(state, "clink");
        lua_pushliteral(state, "_wait_duration");
        lua_rawget(state, -2);

        if (m_state.pcall(state, 0, 1) == 0)
        {
            int32 isnum;
            double sec = lua_tonumberx(state, -1, &isnum);
            if (isnum)
            {
                const DWORD t = (sec > 0) ? uint32(sec * 1000) : 0;
                timeout = min(timeout, t);
            }
        }
    }

    return timeout;
}

//------------------------------------------------------------------------------
static uint32 add_event(void** events, uint32& index, uint32& total, size_t max, void* handle)
{
    uint32 ret = -1;
    if (index < max && handle)
    {
        ret = index;
        events[index++] = handle;
    }
    total++;
    return ret;
}

//------------------------------------------------------------------------------
uint32 lua_input_idle::get_wait_events(void** events, size_t max)
{
    uint32 index = 0;
    uint32 total = 0;
    m_index_recognizer = add_event(events, index, total, max, get_recognizer_event());
    m_index_task_manager = add_event(events, index, total, max, get_task_manager_event());
    m_index_force_idle = add_event(events, index, total, max, get_idle_event());
    assert(total <= max);
    return index;
}

//------------------------------------------------------------------------------
void lua_input_idle::on_wait_event(uint32 index)
{
    if (index == uint32(-1))                assert(false);
    else if (index == m_index_recognizer)   refresh_recognizer();
    else if (index == m_index_task_manager) task_manager_on_idle(m_state);
    else if (index == m_index_force_idle)   on_idle();
    else                                    assert(false);
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

    const bool input_hinter_due = (host_get_input_hint_timeout() == 0);
    if (s_signaled_reclassify || input_hinter_due)
    {
        s_signaled_reclassify = false;
        if (input_hinter_due)
            host_clear_input_hint_timeout();
        reclassify(s_signaled_reclassify ? reclassify_reason::force : reclassify_reason::hinter);
    }
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
HANDLE lua_input_idle::get_idle_event()
{
    static HANDLE s_idle_event = CreateEvent(nullptr, false, false, nullptr);
    return s_idle_event;
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
