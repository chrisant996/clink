// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

//------------------------------------------------------------------------------
template <class T>
class lua_bindable
{
public:
    typedef int         (T::*method_t)(lua_State*);

    struct method
    {
        const char*     name;
        method_t        ptr;
    };

                        lua_bindable(const char* name, const method* methods);
                        ~lua_bindable();
    void                push(lua_State* state);

private:
    static int          call(lua_State* state);
    void                bind();
    void                unbind();
    const char*         m_name;
    const method*       m_methods;
    lua_State*          m_state;
    int                 m_registry_ref;
};

//------------------------------------------------------------------------------
template <class T>
lua_bindable<T>::lua_bindable(const char* name, const method* methods)
: m_name(name)
, m_methods(methods)
, m_state(nullptr)
, m_registry_ref(LUA_NOREF)
{
}

//------------------------------------------------------------------------------
template <class T>
lua_bindable<T>::~lua_bindable()
{
    unbind();
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::bind()
{
    void* self = lua_newuserdata(m_state, sizeof(void*));
    *(void**)self = this;

    str<48> mt_name;
    mt_name << m_name << "_mt";
    if (luaL_newmetatable(m_state, mt_name.c_str()))
    {
        lua_createtable(m_state, 0, 0);

        auto* methods = m_methods;
        while (methods != nullptr && methods->name != nullptr)
        {
            auto* ptr = (method_t*)lua_newuserdata(m_state, sizeof(method_t));
            *ptr = methods->ptr;

            if (luaL_newmetatable(m_state, "lua_bindable"))
            {
                lua_pushcfunction(m_state, &lua_bindable<T>::call);
                lua_setfield(m_state, -2, "__call");
            }

            lua_setmetatable(m_state, -2);
            lua_setfield(m_state, -2, methods->name);

            ++methods;
        }

        lua_setfield(m_state, -2, "__index");
    }

    lua_setmetatable(m_state, -2);
    m_registry_ref = luaL_ref(m_state, LUA_REGISTRYINDEX);
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::unbind()
{
    if (m_state == nullptr || m_registry_ref == LUA_NOREF)
        return;

    lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_registry_ref);
    if (void* self = lua_touserdata(m_state, -1))
        *(void**)self = nullptr;
    lua_pop(m_state, 1);

    luaL_unref(m_state, LUA_REGISTRYINDEX, m_registry_ref);
    m_registry_ref = LUA_NOREF;
    m_state = nullptr;
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::push(lua_State* state)
{
    m_state = state;

    if (m_registry_ref == LUA_NOREF)
        bind();

    lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_registry_ref);
}

//------------------------------------------------------------------------------
template <class T>
int lua_bindable<T>::call(lua_State* state)
{
    auto* const* self = (T* const*)lua_touserdata(state, 2);
    if (self == nullptr || *self == nullptr)
        return 0;

    auto* const ptr = (method_t const*)lua_touserdata(state, 1);
    if (ptr == nullptr)
        return 0;

    lua_remove(state, 1);
    lua_remove(state, 1);

    return ((*self)->*(*ptr))(state);
}
