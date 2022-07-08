// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
/// -name:  path.normalise
/// -ver:   1.1.0
/// -arg:   path:string
/// -arg:   [separator:string]
/// -ret:   string
/// Cleans <span class="arg">path</span> by normalising separators and removing
/// "." and ".." elements.  If <span class="arg">separator</span> is provided it
/// is used to delimit path elements, otherwise a system-specific delimiter is
/// used.
/// -show:  path.normalise("a////b/\\/c/")  -- returns "a\b\c\"
/// -show:  path.normalise("")              -- returns ""
static int normalise(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    int separator = 0;
    if (const char* sep_str = optstring(state, 2, ""))
        separator = sep_str[0];
    else
        return 0;

    str<288> out(path);
    path::normalise(out, separator);
    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getbasename
/// -ver:   1.1.0
/// -arg:   path:string
/// -ret:   string
/// -show:  path.getbasename("/foo/bar.ext")    -- returns "bar"
/// -show:  path.getbasename("")                -- returns ""
static int get_base_name(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<288> out;
    path::get_base_name(path, out);
    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getdirectory
/// -ver:   1.1.0
/// -arg:   path:string
/// -ret:   nil or string
/// This is similar to <a href="#path.toparent">path.toparent()</a> but can
/// behave differently when the input path ends with a path separator.  This is
/// the recommended API for parsing a path into its component pieces, but is not
/// recommended for walking up through parent directories.
/// -show:  path.getdirectory("foo")                -- returns nil
/// -show:  path.getdirectory("\foo")               -- returns "\"
/// -show:  path.getdirectory("c:foo")              -- returns "c:"
/// -show:  path.getdirectory([[c:\]])              -- returns "c:\"
/// -show:  path.getdirectory("c:\foo")             -- returns "c:\"
/// -show:  path.getdirectory("c:\foo\bar")         -- returns "c:\foo"
/// -show:  path.getdirectory("\\foo\bar")          -- returns "\\foo\bar"
/// -show:  path.getdirectory("\\foo\bar\dir")      -- returns "\\foo\bar"
/// -show:  path.getdirectory("")                   -- returns nil
/// -show:
/// -show:  -- These split the path components differently than path.toparent().
/// -show:  path.getdirectory([[c:\foo\bar\]])      -- returns "c:\foo\bar"
/// -show:  path.getdirectory([[\\foo\bar\dir\]])   -- returns "\\foo\bar\dir"
static int get_directory(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<288> out(path);
    if (out.length() == 0)
        return 0;

    if (!path::get_directory(out))
        return 0;

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getdrive
/// -ver:   1.1.0
/// -arg:   path:string
/// -ret:   nil or string
/// -show:  path.getdrive("e:/foo/bar")     -- returns "e:"
/// -show:  path.getdrive("foo/bar")        -- returns nil
/// -show:  path.getdrive("")               -- returns nil
static int get_drive(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<8> out(path);
    if (out.length() == 0)
        return 0;

    if (!path::get_drive(out))
        return 0;

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getextension
/// -ver:   1.1.0
/// -arg:   path:string
/// -ret:   string
/// -show:  path.getextension("bar.ext")    -- returns ".ext"
/// -show:  path.getextension("bar")        -- returns ""
/// -show:  path.getextension("")           -- returns ""
static int get_extension(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<32> ext;
    path::get_extension(path, ext);
    lua_pushlstring(state, ext.c_str(), ext.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.getname
/// -ver:   1.1.0
/// -arg:   path:string
/// -ret:   string
/// -show:  path.getname("/foo/bar.ext")    -- returns "bar.ext"
/// -show:  path.getname("")                -- returns ""
static int get_name(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<> name;
    path::get_name(path, name);
    lua_pushlstring(state, name.c_str(), name.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.join
/// -ver:   1.1.0
/// -arg:   left:string
/// -arg:   right:string
/// -ret:   string
/// If <span class="arg">right</span> is a relative path, this joins
/// <span class="arg">left</span> and <span class="arg">right</span>.
///
/// If <span class="arg">right</span> is not a relative path, this returns
/// <span class="arg">right</span>.
/// -show:  path.join("/foo", "bar")        -- returns "/foo\bar"
/// -show:  path.join("", "bar")            -- returns "bar"
/// -show:  path.join("/foo", "")           -- returns "/foo\"
/// -show:  path.join("/foo", "/bar/xyz")   -- returns "/bar/xyz"
static int join(lua_State* state)
{
    const char* lhs = checkstring(state, 1);
    const char* rhs = checkstring(state, 2);
    if (!lhs || !rhs)
        return 0;

    str<288> out;
    path::join(lhs, rhs, out);
    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.isexecext
/// -ver:   1.1.5
/// -arg:   path:string
/// -ret:   boolean
/// Examines the extension of the path name.  Returns true if the extension is
/// listed in %PATHEXT%.  This caches the extensions in a map so that it's more
/// efficient than getting and parsing %PATHEXT% each time.
/// -show:  path.isexecext("program.exe")   -- returns true
/// -show:  path.isexecext("file.doc")      -- returns false
/// -show:  path.isexecext("")              -- returns false
static int is_exec_ext(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    bool is_exec = path::is_executable_extension(path);
    lua_pushboolean(state, is_exec != false);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.toparent
/// -ver:   1.1.20
/// -arg:   path:string
/// -ret:   parent:string, child:string
/// Splits the last path component from <span class="arg">path</span>, if
/// possible. Returns the result and the component that was split, if any.
///
/// This is similar to <a href="#path.getdirectory">path.getdirectory()</a> but
/// can behave differently when the input path ends with a path separator.  This
/// is the recommended API for walking up through parent directories.
/// -show:  local parent,child
/// -show:  parent,child = path.toparent("foo")             -- returns "", "foo"
/// -show:  parent,child = path.toparent("\foo")            -- returns "\", "foo"
/// -show:  parent,child = path.toparent("c:foo")           -- returns "c:", "foo"
/// -show:  parent,child = path.toparent([[c:\]])           -- returns "c:\", ""
/// -show:  parent,child = path.toparent("c:\foo")          -- returns "c:\", "foo"
/// -show:  parent,child = path.toparent("c:\foo\bar")      -- returns "c:\foo", "bar"
/// -show:  parent,child = path.toparent("\\foo\bar")       -- returns "\\foo\bar", ""
/// -show:  parent,child = path.toparent("\\foo\bar\dir")   -- returns "\\foo\bar", "dir"
/// -show:  parent,child = path.toparent("")                -- returns "", ""
/// -show:
/// -show:  -- These split the path components differently than path.getdirectory().
/// -show:  parent,child = path.toparent([[c:\foo\bar\]])   -- returns "c:\foo", "bar"
/// -show:  parent,child = path.toparent([[\\foo\bar\dir\]])-- returns "\\foo\bar", "dir"
static int to_parent(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<> parent;
    str<> child;
    parent = path;
    path::to_parent(parent, &child);

    lua_pushlstring(state, parent.c_str(), parent.length());
    lua_pushlstring(state, child.c_str(), child.length());
    return 2;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int is_device(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    lua_pushboolean(state, path::is_device(path));
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
        { "isexecext",     &is_exec_ext },
        { "toparent",      &to_parent },
        // UNDOCUMENTED; internal use only.
        { "isdevice",      &is_device },
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
