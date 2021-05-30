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
lua_input_idle::lua_input_idle(lua_state& state)
: m_state(state)
{
}

//------------------------------------------------------------------------------
void lua_input_idle::reset()
{
    m_enabled = true;
    m_event = shared_event::make();
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
TODO("COROUTINES: return INFINITE if all coroutines are associated with events.");
TODO("COROUTINES: return timeout with dynamic throttling otherwise.");
    //return 16;
    return 100;
}

//------------------------------------------------------------------------------
std::shared_ptr<shared_event> lua_input_idle::get_waitevent()
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
