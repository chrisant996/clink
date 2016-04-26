// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/str.h>

//------------------------------------------------------------------------------
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
static int chdir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::set_current_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
static int getcwd(lua_State* state)
{
    str<MAX_PATH> dir;
    os::get_current_dir(dir);

    lua_pushstring(state, dir.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int mkdir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::make_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
static int rmdir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::remove_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
static int isdir(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_dir));
    return 1;
}

//------------------------------------------------------------------------------
static int isfile(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_file));
    return 1;
}

//------------------------------------------------------------------------------
static int remove(lua_State* state)
{
    const char* path = get_string(state, 1);
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
static int rename(lua_State* state)
{
    const char* src = get_string(state, 1);
    const char* dest = get_string(state, 2);
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
static int copy(lua_State* state)
{
    const char* src = get_string(state, 1);
    const char* dest = get_string(state, 2);
    if (src == nullptr || dest == nullptr)
        return 0;

    lua_pushboolean(state, (os::copy(src, dest) == true));
    return 1;
}

//------------------------------------------------------------------------------
static int glob_impl(lua_State* state, bool dirs_only)
{
    const char* mask = get_string(state, 1);
    if (mask == nullptr)
        return 0;

    lua_createtable(state, 0, 0);

    globber globber(mask);
    globber.files(!dirs_only);

    int i = 1;
    str<MAX_PATH> file;
    while (globber.next(file))
    {
        lua_pushstring(state, file.c_str());
        lua_rawseti(state, -2, i++);
    }

    return 1;
}

//------------------------------------------------------------------------------
static int globdirs(lua_State* state)
{
    return glob_impl(state, true);
}

//------------------------------------------------------------------------------
static int globfiles(lua_State* state)
{
    return glob_impl(state, false);
}

//------------------------------------------------------------------------------
static int getenv(lua_State* state)
{
    const char* name = get_string(state, 1);
    if (name == nullptr)
        return 0;

    str<128> value;
    if (!os::get_env(name, value))
        return 0;

    lua_pushstring(state, value.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int getenvnames(lua_State* state)
{
    lua_createtable(state, 0, 0);

    char* root = GetEnvironmentStrings();
    if (root == nullptr)
        return 1;

    char* strings = root;
    int i = 1;
    while (*strings)
    {
        // Skip env vars that start with a '='. They're hidden ones.
        if (*strings == '=')
        {
            strings += strlen(strings) + 1;
            continue;
        }

        char* eq = strchr(strings, '=');
        if (eq == nullptr)
            break;

        *eq = '\0';

        lua_pushstring(state, strings);
        lua_rawseti(state, -2, i++);

        ++eq;
        strings = eq + strlen(eq) + 1;
    }

    FreeEnvironmentStrings(root);
    return 1;
}

//------------------------------------------------------------------------------
void os_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "chdir",      &chdir },
        { "getcwd",     &getcwd },
        { "mkdir",      &mkdir },
        { "rmdir",      &rmdir },
        { "isdir",      &isdir },
        { "isfile",     &isfile },
        { "remove",     &remove },
        { "rename",     &rename },
        { "copy",       &copy },
        { "globdirs",   &globdirs },
        { "globfiles",  &globfiles },
        { "getenv",     &getenv },
        { "getenvnames",&getenvnames },
    };

    lua_State* state = lua.get_state();

    // Add set some methods to the os table.
    lua_getglobal(state, "os");
    for (int i = 0; i < sizeof_array(methods); ++i)
    {
        lua_pushcfunction(state, methods[i].method);
        lua_setfield(state, -2, methods[i].name);
    }
}
