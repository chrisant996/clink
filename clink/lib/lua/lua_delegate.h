// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

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
