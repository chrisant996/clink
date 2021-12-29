// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <assert.h>

//------------------------------------------------------------------------------
// Subclass must define two members:
//  - static const char* const c_name.
//  - static const method c_methods[], which must end with a {} element.
//
// Use make_new() to create a new instance from the Lua heap as a Lua object.
// Lua lifetime semantics control the lifetime (__gc invokes destructor).
//      derived_from_lua_bindable::make_new(state, foo);
//
// Use push() to push a reference; does not require make_new().  C++ lifetime
// semantics control the lifetime (__gc unbinds but does not run destructor).
//      derived_from_lua_bindable bar(foo);
//      bar.push(state);
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

                        lua_bindable();
                        ~lua_bindable();
    template<typename... Args> static T* make_new(lua_State* state, Args... args);
    void                push(lua_State* state);

private:
    static int          call(lua_State* state);
    static void         add_methods(lua_State* state, const method* methods);
    void                make_metatable(lua_State* state);
    void                bind(lua_State* state);
    void                unbind();
    int                 __gc(lua_State* state);
    int                 __tostring(lua_State* state);
    lua_State*          m_state = nullptr;
    int                 m_registry_ref = LUA_NOREF;
    bool                m_owned = false;
};

//------------------------------------------------------------------------------
template <class T>
lua_bindable<T>::lua_bindable()
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
void lua_bindable<T>::add_methods(lua_State* state, const method* methods)
{
    while (methods != nullptr && methods->name != nullptr)
    {
        auto* ptr = (method_t*)lua_newuserdata(state, sizeof(method_t));
        *ptr = methods->ptr;

        if (luaL_newmetatable(state, "lua_bindable"))
        {
            lua_pushliteral(state, "__call");
            lua_pushcfunction(state, &lua_bindable<T>::call);
            lua_rawset(state, -3);
        }

        lua_setmetatable(state, -2);
        lua_setfield(state, -2, methods->name);

        ++methods;
    }
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::make_metatable(lua_State* state)
{
    if (luaL_newmetatable(state, T::c_name))
    {
        static const T::method c_default_methods[] = {
            { "__gc",       &T::__gc },
            { "__tostring", &T::__tostring },
            { nullptr,      nullptr }
        };

        lua_createtable(state, 0, 0);

        add_methods(state, T::c_methods);
        add_methods(state, c_default_methods);

        lua_setfield(state, -2, "__index");
    }

    lua_setmetatable(state, -2);
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::bind(lua_State* state)
{
    assert(!m_owned);
    assert(m_state == nullptr);
    assert(m_registry_ref == LUA_NOREF);

#ifdef DEBUG
    int oldtop = lua_gettop(state);
#endif

    void* self = lua_newuserdata(state, sizeof(void*));
    *(void**)self = this;

    make_metatable(state);

#ifdef DEBUG
    assert(oldtop + 1 == lua_gettop(state));
#endif

    m_state = state;
    m_registry_ref = luaL_ref(state, LUA_REGISTRYINDEX);

#ifdef DEBUG
    assert(oldtop == lua_gettop(state));
#endif
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
template <typename... Args>
T* lua_bindable<T>::make_new(lua_State* state, Args... args)
{
#ifdef DEBUG
    int oldtop = lua_gettop(state);
#endif

    auto** self = (T**)lua_newuserdata(state, sizeof(T*));
    *self = new T(args...);
    (*self)->m_owned = true;

#ifdef DEBUG
    assert(oldtop + 1 == lua_gettop(state));
#endif

    (*self)->make_metatable(state);

#ifdef DEBUG
    int newtop = lua_gettop(state);
    assert(oldtop + 1 == newtop);
    auto* const* test = (T* const*)luaL_checkudata(state, -1, T::c_name);
    assert(test == self);
    assert(*test == *self);
#endif

    return *self;
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::push(lua_State* state)
{
#ifdef DEBUG
    int top = lua_gettop(state);
#endif

    if (m_registry_ref == LUA_NOREF)
        bind(state);

    lua_rawgeti(state, LUA_REGISTRYINDEX, m_registry_ref);

#ifdef DEBUG
    assert(top + 1 == lua_gettop(state));
#endif
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

//------------------------------------------------------------------------------
template <class T>
int lua_bindable<T>::__gc(lua_State* state)
{
    auto* const* self = (T* const*)luaL_checkudata(state, 1, T::c_name);
    if (self && *self && (*self)->m_owned)
        delete *self;
    return 0;
}

//------------------------------------------------------------------------------
template <class T>
int lua_bindable<T>::__tostring(lua_State* state)
{
    auto* const* self = (T* const*)luaL_checkudata(state, 1, T::c_name);
    lua_pushfstring(state, "%s (%p %p)", T::c_name, self, self ? *self : nullptr);
    return 1;
}
