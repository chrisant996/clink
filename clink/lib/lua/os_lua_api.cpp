// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "os_lua_api.h"
#include "core/base.h"
#include "core/str.h"
#include "core/os.h"
#include "lua/lua_delegate.h"

//------------------------------------------------------------------------------
void os_lua_api::initialise(lua_State* state)
{
    struct {
        const char* name;
        int         (os_lua_api::*method)(lua_State*);
    } methods[] = {
        { "chdir",      &os_lua_api::chdir },
        { "getcwd",     &os_lua_api::getcwd },
        { "mkdir",      &os_lua_api::mkdir },
        { "rmdir",      &os_lua_api::rmdir },
        { "remove",     &os_lua_api::remove },
        { "rename",     &os_lua_api::rename },
        { "copy",       &os_lua_api::copy },
        { "isdir",      &os_lua_api::isdir },
        { "isfile",     &os_lua_api::isfile },
        { "getenv",     &os_lua_api::getenv },
    };

    // Add set some methods to the os table.
    lua_getglobal(state, "os");
    for (int i = 0; i < sizeof_array(methods); ++i)
    {
        lua_delegate::push(state, this, methods[i].method);
        lua_setfield(state, -2, methods[i].name);
    }
}

//------------------------------------------------------------------------------
const char* os_lua_api::get_string(lua_State* state, int index)
{
    if (lua_gettop(state) <= index || lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
int os_lua_api::chdir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state))
        ok = os::set_current_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
int os_lua_api::getcwd(lua_State* state)
{
    str<MAX_PATH> dir;
    os::get_current_dir(dir);

    lua_pushstring(state, dir.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int os_lua_api::mkdir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state))
        ok = os::make_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
int os_lua_api::rmdir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state))
        ok = os::remove_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
int os_lua_api::isdir(lua_State* state)
{
    const char* path = get_string(state);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_dir));
    return 1;
}

//------------------------------------------------------------------------------
int os_lua_api::isfile(lua_State* state)
{
    const char* path = get_string(state);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_file));
    return 1;
}

//------------------------------------------------------------------------------
int os_lua_api::remove(lua_State* state)
{
    const char* path = get_string(state);
    if (path == nullptr)
        return 0;

    if (os::unlink(path))
    {
        lua_pushboolean(state, 1);
        return 1;
    }

    lua_pushnil(state);
    lua_pushstring(state, "error");
    lua_pushinteger(state, 1);
    return 3;
}

//------------------------------------------------------------------------------
int os_lua_api::rename(lua_State* state)
{
    const char* src = get_string(state, 0);
    const char* dest = get_string(state, 1);
    if (src != nullptr && dest != nullptr && os::move(src, dest))
    {
        lua_pushboolean(state, 1);
        return 1;
    }

    lua_pushnil(state);
    lua_pushstring(state, "error");
    lua_pushinteger(state, 1);
    return 3;
}

//------------------------------------------------------------------------------
int os_lua_api::copy(lua_State* state)
{
    const char* src = get_string(state, 0);
    const char* dest = get_string(state, 1);
    if (src == nullptr || dest == nullptr)
        return 0;

    lua_pushboolean(state, (os::copy(src, dest) == true));
    return 1;
}

//------------------------------------------------------------------------------
int os_lua_api::getenv(lua_State* state)
{
    const char* name = get_string(state);
    if (name == nullptr)
        return 0;

    str<128> value;
    if (!os::get_env(name, value))
        return 0;

    lua_pushstring(state, value.c_str());
    return 1;
}
