// Copyright (c) 2021 Christopher Antos
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
// Sample usage:
//
//      class foo_lua : public lua_makeable<foo_lua>
//      {
//      public:
//          foo_lua(int x);
//      private:
//          friend class lua_makeable<foo_lua>;
//          static const char* const c_name;
//          static const method c_methods[];
//          int getx(lua_State* state);
//          int setx(lua_State* state);
//          int m_x;
//      }
//
//      const char* const foo_lua::c_name = "foo_lua";
//
//      const method foo_lua::c_methods[] = {
//          { "getx", &getx },
//          { "setx", &setx },
//          {}
//      }
//
//      foo_lua::foo_lua(int x) : m_x(x) {}
//
//      int foo_lua::getx(lua_State* state)
//      {
//          lua_pushinteger(state, m_x);
//          return 1;
//      }
//
//      int foo_lua::setx(lua_State* state)
//      {
//          m_x = lua_tointeger(state, 1);
//          return 0;
//      }

template <class T>
class lua_makeable
{
public:
    typedef int         (T::*method_t)(lua_State*);

    struct method
    {
        const char*     name;
        method_t        ptr;
    };

    template<typename... Args> static T* make_new(lua_State* state, Args... args);

private:
                        lua_makeable();
                        ~lua_makeable();
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
lua_makeable<T>::lua_makeable()
{
}

//------------------------------------------------------------------------------
template <class T>
lua_makeable<T>::~lua_makeable()
{
}

//------------------------------------------------------------------------------
template <class T>
void lua_makeable<T>::add_methods(lua_State* state, const method* methods)
{
    while (methods != nullptr && methods->name != nullptr)
    {
        auto* ptr = (method_t*)lua_newuserdata(state, sizeof(method_t));
        *ptr = methods->ptr;

        if (luaL_newmetatable(state, "lua_makeable"))
        {
            lua_pushliteral(state, "__call");
            lua_pushcfunction(state, &lua_makeable<T>::call);
            lua_rawset(state, -3);
        }

        lua_setmetatable(state, -2);
        lua_setfield(state, -2, methods->name);

        ++methods;
    }
}

//------------------------------------------------------------------------------
template <class T>
void lua_makeable<T>::make_metatable(lua_State* state)
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
template <typename... Args>
T* lua_makeable<T>::make_new(lua_State* state, Args... args)
{
#ifdef DEBUG
    int oldtop = lua_gettop(state);
#endif

    T* self = (T*)lua_newuserdata(state, sizeof(T));
    new (self) T(args...);

#ifdef DEBUG
    assert(oldtop + 1 == lua_gettop(state));
#endif

    self->make_metatable(state);

#ifdef DEBUG
    int newtop = lua_gettop(state);
    assert(oldtop + 1 == newtop);
    T* test = (T*)luaL_checkudata(state, -1, T::c_name);
    assert(test == self);
#endif

    return self;
}

//------------------------------------------------------------------------------
template <class T>
int lua_makeable<T>::call(lua_State* state)
{
    T* self = (T*)lua_touserdata(state, 2);
    if (self == nullptr)
        return 0;

    auto* const ptr = (method_t const*)lua_touserdata(state, 1);
    if (ptr == nullptr)
        return 0;

    lua_remove(state, 1);
    lua_remove(state, 1);

    return (self->*(*ptr))(state);
}

//------------------------------------------------------------------------------
template <class T>
int lua_makeable<T>::__gc(lua_State* state)
{
    T* self = (T*)luaL_checkudata(state, 1, T::c_name);
    self->~T();
    return 0;
}

//------------------------------------------------------------------------------
template <class T>
int lua_makeable<T>::__tostring(lua_State* state)
{
    T* self = (T*)luaL_checkudata(state, 1, T::c_name);
    lua_pushfstring(state, "%s (%p)", T::c_name, self);
    return 1;
}
