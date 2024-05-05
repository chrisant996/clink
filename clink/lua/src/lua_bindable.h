// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/object.h>
#include <core/str.h>
#include <core/settings.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <assert.h>

//------------------------------------------------------------------------------
extern setting_bool g_lua_strict;

//------------------------------------------------------------------------------
// The lua_bindable<T> template binds a C++ class into a Lua object.  Its
// lifetime is managed by either C++ or by Lua garbage collection, depending on
// whether push() or make_new() is used, respectively.
//
// Use make_new() to create a new instance from the Lua heap as a Lua object.
// Lua lifetime semantics control the lifetime (__gc invokes destructor).
//      derived_from_lua_bindable::make_new(state, foo);
//
// Use push() to push a reference; does not require make_new(), but can be used
// in conjunction with it.  C++ lifetime semantics control the lifetime (__gc
// unbinds but does not run destructor).
//      derived_from_lua_bindable bar(foo);
//      bar.push(state);
//
// A subclass must define two members:
//  - static const char* const c_name.
//  - static const method c_methods[], which must end with a {} element.
template <class T>
class lua_bindable
#ifdef USE_DEBUG_OBJECT
: public object
#endif
{
public:
    typedef int32       (T::*method_t)(lua_State*);

    struct method
    {
        const char*     name;
        method_t        ptr;
    };

                        lua_bindable();
                        ~lua_bindable();
    template<typename... Args> static T* make_new(lua_State* state, Args... args);
    void                push(lua_State* state);
    static T*           check(lua_State* state, int32 idx);
    static T*           test(lua_State* state, int32 idx);

private:
    static int32        call(lua_State* state);
    static int32        __gc(lua_State* state);
    static int32        __tostring(lua_State* state);
    void                make_metatable(lua_State* state);
    void                bind(lua_State* state);
    void                unbind();
    lua_State*          m_state_unbind = nullptr;
    int32               m_registry_ref = LUA_NOREF;
    bool                m_owned = false;
#ifdef DEBUG
    bool                m_deleteable = true;
#endif
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
    // For owned objects (make_new), support currently exists only for deleting
    // via garbage collection.
    assert(m_deleteable);

    unbind();
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::make_metatable(lua_State* state)
{
    if (luaL_newmetatable(state, T::c_name))
    {
        // Add __gc and __tostring directly to metatable.

        lua_pushcfunction(state, &T::__gc);
        lua_setfield(state, -2, "__gc");

        lua_pushcfunction(state, &T::__tostring);
        lua_setfield(state, -2, "__tostring");

        // Add other methods to __index table.

        lua_createtable(state, 0, 0);

        str<> metaname;
        metaname.format("lua_bindable<%s>", T::c_name);

        const method* methods = T::c_methods;
        while (methods != nullptr && methods->name != nullptr)
        {
            auto* ptr = (method_t*)lua_newuserdata(state, sizeof(method_t));
            *ptr = methods->ptr;

            if (luaL_newmetatable(state, metaname.c_str()))
            {
                lua_pushliteral(state, "__call");
                lua_pushcfunction(state, &lua_bindable<T>::call);
                lua_rawset(state, -3);
            }

            lua_setmetatable(state, -2);
            lua_setfield(state, -2, methods->name);

            ++methods;
        }

        lua_setfield(state, -2, "__index");
    }

    lua_setmetatable(state, -2);
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::bind(lua_State* state)
{
    assert(m_state_unbind == nullptr);
    assert(m_registry_ref == LUA_NOREF);

#ifdef DEBUG
    int32 oldtop = lua_gettop(state);
#endif

    auto** self = (T**)lua_newuserdata(state, sizeof(void*));
    *self = static_cast<T*>(this);

    make_metatable(state);

#ifdef DEBUG
    assert(oldtop + 1 == lua_gettop(state));
#endif

    m_state_unbind = state;
    m_registry_ref = luaL_ref(state, LUA_REGISTRYINDEX);

#ifdef DEBUG
    assert(oldtop == lua_gettop(state));
#endif
}

//------------------------------------------------------------------------------
template <class T>
void lua_bindable<T>::unbind()
{
    if (m_state_unbind == nullptr || m_registry_ref == LUA_NOREF)
        return;

    lua_rawgeti(m_state_unbind, LUA_REGISTRYINDEX, m_registry_ref);
    if (auto** self = (T**)lua_touserdata(m_state_unbind, -1))
        *self = nullptr;
    lua_pop(m_state_unbind, 1);

    luaL_unref(m_state_unbind, LUA_REGISTRYINDEX, m_registry_ref);
    m_registry_ref = LUA_NOREF;
    m_state_unbind = nullptr;
}

//------------------------------------------------------------------------------
template <class T>
template <typename... Args>
T* lua_bindable<T>::make_new(lua_State* state, Args... args)
{
#ifdef DEBUG
    int32 oldtop = lua_gettop(state);
#endif

    auto** self = (T**)lua_newuserdata(state, sizeof(T*));
    *self = new T(args...);
    (*self)->m_owned = true;
#ifdef DEBUG
    (*self)->m_deleteable = false;
#endif

#ifdef DEBUG
    assert(oldtop + 1 == lua_gettop(state));
#endif

    (*self)->make_metatable(state);

#ifdef DEBUG
    int32 newtop = lua_gettop(state);
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
    int32 top = lua_gettop(state);
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
T* lua_bindable<T>::check(lua_State* state, int32 idx)
{
    auto* const* self = (T* const*)luaL_checkudata(state, idx, T::c_name);
    if (self && *self)
        return *self;
    return 0;
}

//------------------------------------------------------------------------------
template <class T>
T* lua_bindable<T>::test(lua_State* state, int32 idx)
{
    auto* const* self = (T* const*)luaL_testudata(state, idx, T::c_name);
    if (self && *self)
        return *self;
    return 0;
}

//------------------------------------------------------------------------------
template <class T>
int32 lua_bindable<T>::call(lua_State* state)
{
    auto* const* self = (T* const*)luaL_checkudata(state, 2, T::c_name);
    if (self == nullptr || *self == nullptr)
    {
        if (g_lua_strict.get())
            return luaL_error(state, "object %s (%p %p) is not bound", T::c_name, self, self ? *self : nullptr);
        return 0;
    }

    auto* const ptr = (method_t const*)lua_touserdata(state, 1);
    if (ptr == nullptr)
    {
        if (g_lua_strict.get())
            return luaL_error(state, "attempt to call a nil method on %s (%p %p)", T::c_name, self, self ? *self : nullptr);
        return 0;
    }

#ifdef DEBUG
    const int32 top = lua_gettop(state);
#endif

    // Don't remove self before calling the method!  Removing it is convenient
    // for the author of native C/C++ code in Clink, but it throws off all the
    // arg# error reporting.  Use LUA_SELF + 1 to refer to arg #1, etc.
    lua_remove(state, 1);
    const int32 ret = ((*self)->*(*ptr))(state);

    // Now remove self to clean up the stack.
    lua_remove(state, 1);

#ifdef DEBUG
    const int32 new_top = lua_gettop(state);
    assert(new_top == top - 2 + ret);
#endif

    return ret;
}

//------------------------------------------------------------------------------
template <class T>
int32 lua_bindable<T>::__gc(lua_State* state)
{
    auto* const* self = (T* const*)luaL_checkudata(state, 1, T::c_name);
    if (self && *self && (*self)->m_owned)
    {
#ifdef DEBUG
        (*self)->m_deleteable = true;
#endif
        delete *self;
    }
    return 0;
}

//------------------------------------------------------------------------------
template <class T>
int32 lua_bindable<T>::__tostring(lua_State* state)
{
    auto* const* self = (T* const*)luaL_checkudata(state, 1, T::c_name);
    lua_pushfstring(state, "%s (%p %p)", T::c_name, self, self ? *self : nullptr);
    return 1;
}
