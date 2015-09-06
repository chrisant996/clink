// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "path_lua_api.h"
#include "core/base.h"
#include "core/path.h"
#include "core/str.h"

//------------------------------------------------------------------------------
void path_lua_api::initialise(struct lua_State* state)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "clean",         &path_lua_api::clean },
        { "getbasename",   &path_lua_api::get_base_name },
        { "getdirectory",  &path_lua_api::get_directory },
        { "getdrive",      &path_lua_api::get_drive },
        { "getextension",  &path_lua_api::get_extension },
        { "getname",       &path_lua_api::get_name },
        { "join",          &path_lua_api::join },
    };

    lua_createtable(state, 0, 0);

    for (int i = 0; i < sizeof_array(methods); ++i)
    {
        lua_pushcfunction(state, methods[i].method);
        lua_setfield(state, -2, methods[i].name);
    }

    lua_setglobal(state, "path");
}

//------------------------------------------------------------------------------
const char* path_lua_api::get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
int path_lua_api::clean(lua_State* state)
{
    str<MAX_PATH> out = get_string(state, 1);
    if (out.length() == 0)
        return 0;

    const char* separator = get_string(state, 2);
    if (separator == nullptr)
        separator = "\\";

    path::clean(out, separator[0]);
    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int path_lua_api::get_base_name(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    str<MAX_PATH> out;
    path::get_base_name(path, out);
    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int path_lua_api::get_directory(lua_State* state)
{
    str<MAX_PATH> out = get_string(state, 1);
    if (out.length() == 0)
        return 0;

    if (!path::get_directory(out))
        return 0;

    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int path_lua_api::get_drive(lua_State* state)
{
    str<8> out = get_string(state, 1);
    if (out.length() == 0)
        return 0;

    if (!path::get_drive(out))
        return 0;

    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int path_lua_api::get_extension(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    str<32> ext;
    path::get_extension(path, ext);
    lua_pushstring(state, ext.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int path_lua_api::get_name(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    str<> name;
    path::get_name(path, name);
    lua_pushstring(state, name.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int path_lua_api::join(lua_State* state)
{
    const char* lhs = get_string(state, 1);
    if (lhs == nullptr)
        return 0;

    const char* rhs = get_string(state, 2);
    if (rhs == nullptr)
        return 0;

    str<MAX_PATH> out;
    path::join(lhs, rhs, out);
    lua_pushstring(state, out.c_str());
    return 1;
}
