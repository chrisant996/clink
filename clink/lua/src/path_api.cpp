// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
/// -name:  path.normalise
/// -arg:   path:string
/// -arg:   [separator:string]
/// -ret:   string
/// -show:  path.normalise("a////b/\\/c/") -- returns "a\b\c\"
/// Cleans <em>path</em> by normalising separators and removing ".[.]/"
/// elements. If <em>separator</em> is provided it is used to delimit path
/// elements, otherwise a system-specific delimiter is used.
static int normalise(lua_State* state)
{
    str<288> out(get_string(state, 1));
    if (out.length() == 0)
        return 0;

    int separator = 0;
    if (const char* sep_str = get_string(state, 2))
        separator = sep_str[0];

    path::normalise(out, separator);
    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getbasename
/// -arg:   path:string
/// -ret:   string
/// -show:  path.getbasename("/foo/bar.ext") -- returns "bar"
static int get_base_name(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    str<288> out;
    path::get_base_name(path, out);
    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getdirectory
/// -arg:   path:string
/// -ret:   nil or string
/// -show:  path.getdirectory("/foo/bar") -- returns "/foo/"
/// -show:  path.getdirectory("bar") -- returns nil
static int get_directory(lua_State* state)
{
    str<288> out(get_string(state, 1));
    if (out.length() == 0)
        return 0;

    if (!path::get_directory(out))
        return 0;

    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getdrive
/// -arg:   path:string
/// -ret:   nil or string
/// -show:  path.getdrive("e:/foo/bar") -- returns "e:"
/// -show:  path.getdrive("foo/bar") -- returns nil
static int get_drive(lua_State* state)
{
    str<8> out(get_string(state, 1));
    if (out.length() == 0)
        return 0;

    if (!path::get_drive(out))
        return 0;

    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getextension
/// -arg:   path:string
/// -ret:   string
/// -show:  path.getextension("bar.ext") -- returns ".ext"
/// -show:  path.getextension("bar") -- returns ""
static int get_extension(lua_State* state)
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
/// -name:  path.getname
/// -arg:   path:string
/// -ret:   string
/// -show:  path.getname("/foo/bar.ext") -- returns "bar.ext"
static int get_name(lua_State* state)
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
/// -name:  path.join
/// -arg:   left:string
/// -arg:   right:string
/// -ret:   string
/// -show:  path.join("/foo", "bar") -- returns "/foo\bar"
static int join(lua_State* state)
{
    const char* lhs = get_string(state, 1);
    if (lhs == nullptr)
        return 0;

    const char* rhs = get_string(state, 2);
    if (rhs == nullptr)
        return 0;

    str<288> out;
    path::join(lhs, rhs, out);
    lua_pushstring(state, out.c_str());
    return 1;
}

//------------------------------------------------------------------------------
void path_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "normalise",     &normalise },
        { "getbasename",   &get_base_name },
        { "getdirectory",  &get_directory },
        { "getdrive",      &get_drive },
        { "getextension",  &get_extension },
        { "getname",       &get_name },
        { "join",          &join },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "path");
}
