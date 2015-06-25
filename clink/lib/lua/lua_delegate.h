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

#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

//------------------------------------------------------------------------------
class lua_delegate
{
public:
    template <class T>
    static void         push(lua_State* state, T* t, int (T::*m)(lua_State*));

private:
    typedef int         (lua_delegate::*method_t)(lua_State*);
    lua_delegate*       self;
    method_t            method;
    static int          call(lua_State* state);
    static const char*  get_metatable_name();
};

//------------------------------------------------------------------------------
template <class T>
void lua_delegate::push(lua_State* state, T* t, int (T::*m)(lua_State*))
{
    if (luaL_newmetatable(state, get_metatable_name()))
    {
        lua_pushcfunction(state, &lua_delegate::call);
        lua_setfield(state, -2, "__call");
    }
    lua_pop(state, 1);

    auto* d = (lua_delegate*)lua_newuserdata(state, sizeof(lua_delegate));
    luaL_setmetatable(state, get_metatable_name());

    d->self = (lua_delegate*)t;
    d->method = (method_t)m;
}

//------------------------------------------------------------------------------
inline int lua_delegate::call(lua_State* state)
{
    void* addr = luaL_checkudata(state, 1, get_metatable_name());
    const auto* d = (lua_delegate*)addr;
    if (d == nullptr)
        return 0;

    lua_remove(state, 1);
    return ((d->self)->*(d->method))(state);
}

//------------------------------------------------------------------------------
inline const char* lua_delegate::get_metatable_name()
{
    return "_luadel_mt";
}
