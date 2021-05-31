// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_input_idle.h"
#include "lua_state.h"

#include <core/base.h>

#include <assert.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

//------------------------------------------------------------------------------
extern void set_io_wake_event(HANDLE event);

//------------------------------------------------------------------------------
lua_input_idle::lua_input_idle(lua_state& state)
: m_state(state)
{
}

//------------------------------------------------------------------------------
lua_input_idle::~lua_input_idle()
{
    set_io_wake_event(nullptr);
}

//------------------------------------------------------------------------------
void lua_input_idle::reset()
{
    HANDLE old_event = m_event;

    // Create new event before closing old handle, to prevent the OS from
    // reusing the same event handle after it's closed.
    m_enabled = true;
    m_iterations = 0;
    m_event = CreateEvent(nullptr, false, false, nullptr);
    set_io_wake_event(m_event);

    if (old_event)
        CloseHandle(old_event);
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
    m_iterations++;

    lua_State* state = m_state.get_state();
    save_stack_top ss(state);

    // Call to Lua to check for coroutines.
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_wait_duration");
    lua_rawget(state, -2);

    if (m_state.pcall(state, 0, 1) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            m_state.print_error(error);

        return INFINITE;
    }

    int isnum;
    double sec = lua_tonumberx(state, -1, &isnum);
    if (!isnum)
        return INFINITE;

    return (sec > 0) ? unsigned(sec * 1000) : 0;
}

//------------------------------------------------------------------------------
void* lua_input_idle::get_waitevent()
{
    if (!m_enabled)
        return nullptr;

    return m_event;
}

//------------------------------------------------------------------------------
void lua_input_idle::on_idle()
{
    assert(m_enabled);

    resume_coroutines();
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
    {
        if (const char* error = lua_tostring(state, -1))
            m_state.print_error(error);

        return false;
    }

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

    if (m_state.pcall(state, 0, 0) != 0)
    {
        if (const char* error = lua_tostring(state, -1))
            m_state.print_error(error);

        return;
    }
}
