/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "lua_root.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

//------------------------------------------------------------------------------
lua_root::lua_root()
: m_state(nullptr)
{
    initialise();
}

//------------------------------------------------------------------------------
lua_root::~lua_root()
{
    shutdown();
}

//------------------------------------------------------------------------------
void lua_root::initialise()
{
    shutdown();

    // Create a new Lua state.
    m_state = luaL_newstate();
    luaL_openlibs(m_state);

    m_clink.initialise(m_state);
    lua_match_generator::initialise(m_state);

    extern const char* lib_script_prompt_lua;
    luaL_dostring(m_state, lib_script_prompt_lua);

    extern const char* lib_script_arguments_lua;
    luaL_dostring(m_state, lib_script_arguments_lua);
}

//------------------------------------------------------------------------------
void lua_root::shutdown()
{
    if (m_state == nullptr)
        return;

    lua_match_generator::shutdown();
    lua_close(m_state);
    m_state = nullptr;
}

//------------------------------------------------------------------------------
bool lua_root::do_string(const char* string)
{
    if (luaL_dostring(m_state, string))
        return false;

    return true;
}

//------------------------------------------------------------------------------
bool lua_root::do_file(const char* path)
{
    if (luaL_dofile(m_state, path))
        return false;

    return true;
}
