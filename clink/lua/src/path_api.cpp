// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <wildmatch/wildmatch.h>

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
/// -show:  path.normalise("a////b/\\/c/")  -- returns "a\\b\\c\\"
/// -show:  path.normalise("./foo/../bar")  -- returns "bar"
/// -show:  path.normalise("")              -- returns ""
static int32 normalise(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    int32 separator = 0;
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
static int32 get_base_name(lua_State* state)
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
/// -show:  path.getdirectory("\\foo")              -- returns "\\"
/// -show:  path.getdirectory("c:foo")              -- returns "c:"
/// -show:  path.getdirectory("c:\\")               -- returns "c:\\"
/// -show:  path.getdirectory("c:\\foo")            -- returns "c:\\"
/// -show:  path.getdirectory("c:\\foo\\bar")       -- returns "c:\\foo"
/// -show:  path.getdirectory("\\\\foo\\bar")       -- returns "\\\\foo\\bar"
/// -show:  path.getdirectory("\\\\foo\\bar\\dir")  -- returns "\\\\foo\\bar"
/// -show:  path.getdirectory("")                   -- returns nil
/// -show:
/// -show:  -- These split the path components differently than path.toparent().
/// -show:  path.getdirectory("c:\\foo\\bar\\")         -- returns "c:\\foo\\bar"
/// -show:  path.getdirectory("\\\\foo\\bar\\dir\\")    -- returns "\\\\foo\\bar\\dir"
static int32 get_directory(lua_State* state)
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
static int32 get_drive(lua_State* state)
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
static int32 get_extension(lua_State* state)
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
static int32 get_name(lua_State* state)
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
/// -show:  path.join("/foo", "bar")        -- returns "/foo\\bar"
/// -show:  path.join("", "bar")            -- returns "bar"
/// -show:  path.join("/foo", "")           -- returns "/foo\\"
/// -show:  path.join("/foo", "/bar/xyz")   -- returns "/bar/xyz"
static int32 join(lua_State* state)
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
static int32 is_exec_ext(lua_State* state)
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
/// -show:  parent,child = path.toparent("foo")                 -- returns "", "foo"
/// -show:  parent,child = path.toparent("\\foo")               -- returns "\\", "foo"
/// -show:  parent,child = path.toparent("c:foo")               -- returns "c:", "foo"
/// -show:  parent,child = path.toparent("c:\\"])               -- returns "c:\\", ""
/// -show:  parent,child = path.toparent("c:\\foo")             -- returns "c:\\", "foo"
/// -show:  parent,child = path.toparent("c:\\foo\\bar")        -- returns "c:\\foo", "bar"
/// -show:  parent,child = path.toparent("\\\\foo\\bar")        -- returns "\\\\foo\\bar", ""
/// -show:  parent,child = path.toparent("\\\\foo\\bar\\dir")   -- returns "\\\\foo\\bar", "dir"
/// -show:  parent,child = path.toparent("")                    -- returns "", ""
/// -show:
/// -show:  -- These split the path components differently than path.getdirectory().
/// -show:  parent,child = path.toparent("c:\\foo\\bar\\"")     -- returns "c:\\foo", "bar"
/// -show:  parent,child = path.toparent("\\\\foo\\bar\\dir\\") -- returns "\\\\foo\\bar", "dir"
static int32 to_parent(lua_State* state)
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
/// -name:  path.fnmatch
/// -ver:   1.4.24
/// -arg:   pattern:string
/// -arg:   string:string
/// -arg:   [flags:string]
/// -ret:   boolean
/// This compares the two strings <span class="arg">pattern</span> and
/// <span class="arg">string</span> and returns whether they are considered to
/// match.  This is like the Linux <code>fnmatch</code> function, with an
/// additional optional mode that can allow matching <code>**</code> patterns
/// <a href="https://git-scm.com/docs/gitignore#_pattern_format">the same as git
/// does</a>.
///
/// The optional <span class="arg">flags</span> string may contain any of the
/// following characters to modify the behavior accordingly:
/// <table>
/// <tr><th>Flag</th><th>Mnemonic</th><th>Description</th></tr>
/// <tr><td>"<code>e</code>"</td><td>NoEscape</td><td>Treat backslash in <span class="arg">pattern</span> as a normal character, rather than as an escape character.</td></tr>
/// <tr><td>"<code>p</code>"</td><td>PathName</td><td>Path separators in <span class="arg">string</span> are matched only by a slash <code>/</code> in <span class="arg">pattern</span> (unless the <code>*</code> flag is used; see below).</td></tr>
/// <tr><td>"<code>.</code>"</td><td>Period</td><td>A leading period <code>.</code> in <span class="arg">string</span> is matched only by a period <code>.</code> in <span class="arg">pattern</span>.  A leading period is one at the beginning of <span class="arg">string</span>, or immediately following a path separator when the <code>p</code> flag is used.</td></tr>
/// <tr><td>"<code>l</code>"</td><td>LeadingDir</td><td>Consider <span class="arg">pattern</span> to be matched if it completely matches <span class="arg">string</span>, or if it matches <span class="arg">string</span> up to a path separator.</td></tr>
/// <tr><td>"<code>c</code>"</td><td>NoCaseFold</td><td>Match with case sensitivity. By default it matches case-insensitively, because Windows is case-insensitive.</td></tr>
/// <tr><td>"<code>*</code>"</td><td>WildStar</td><td>Treat double-asterisks in <span class="arg">pattern</span> as matching path separators as well, the same as how git does (implies the <code>p</code> flag).</td></tr>
/// <tr><td>"<code>s</code>"</td><td>NoSlashFold</td><td>Treat slashes <code>/</code> in <span class="arg">pattern</span> as only matching slashes in <span class="arg">string</span>. By default slashes in <span class="arg">pattern</span> match both slash <code>/</code> and backslash <code>\</code> because Windows recognizes both as path separators.</td></tr>
/// </table>
///
/// The <span class="arg">pattern</span> supports wildcards (<code>?</code> and
/// <code>*</code>), character classes (<code>[</code>...<code>]</code>), ranges
/// (<code>[</code>.<code>-</code>.<code>]</code>), and complementation
/// (<code>[!</code>...<code>]</code> and
/// <code>[!</code>.<code>-</code>.<code>]</code>).
///
/// The <span class="arg">pattern</span> also supports the following character
/// classes:
/// <ul>
/// <li>"<code>[[:alnum:]]</code>": Matches any alphabetic character or digit a - z, or A - Z, or 0 - 9.
/// <li>"<code>[[:alpha:]]</code>": Matches any alphabetic character a - z or A - Z.
/// <li>"<code>[[:blank:]]</code>": Matches 0x20 (space) or 0x09 (tab).
/// <li>"<code>[[:cntrl:]]</code>": Matches 0x00 - 0x1F or 0x7F.
/// <li>"<code>[[:digit:]]</code>": Matches any of the digits 0 - 9.
/// <li>"<code>[[:graph:]]</code>": Matches any character that matches <code>[[:print:]]</code> but does not match <code>[[:space:]]</code>.
/// <li>"<code>[[:lower:]]</code>": Matches any lower case ASCII letter a - z.
/// <li>"<code>[[:print:]]</code>": Matches any printable character (e.g. 0x20 - 0x7E).
/// <li>"<code>[[:punct:]]</code>": Matches any character that matches <code>[[:print:]]</code> but does not match <code>[[:alnum:]]</code>, <code>[[:space:]]</code>, or <code>[[:alnum:]]</code>.
/// <li>"<code>[[:space:]]</code>": Matches 0x20 (space) or 0x09 - 0x0D (tab, linefeed, carriage return, etc).
/// <li>"<code>[[:xdigit:]]</code>": Matches any of the hexadecimal digits 0 - 9, A - F, or a - f.
/// <li>"<code>[[:upper:]]</code>": Matches any upper case ASCII letter A - Z.
/// </ul>
///
/// <strong>Note:</strong> At this time the character classes and
/// case-insensitivity operate on one byte at a time, so they do not fully
/// work as expected with non-ASCII characters.
static int32 api_fnmatch(lua_State* state)
{
    const char* pattern = checkstring(state, 1);
    const char* string = checkstring(state, 2);
    const char* flags = optstring(state, 3, "");

    int32 bits = WM_CASEFOLD|WM_SLASHFOLD;
    for (; *flags; ++flags)
    {
        switch (*flags)
        {
        case 'e':   bits |= WM_NOESCAPE; break;
        case 'p':   bits |= WM_PATHNAME; break;
        case '.':   bits |= WM_PERIOD; break;
        case 'l':   bits |= WM_LEADING_DIR; break;
        case 'c':   bits &= ~WM_CASEFOLD; break;
        case '*':   bits |= WM_WILDSTAR; break;
        case 's':   bits &= ~WM_SLASHFOLD; break;
        default:
            return luaL_argerror(state, 3, "invalid flags");
        }
    }

    bool match = (wildmatch(pattern, string, bits) == WM_MATCH);
    lua_pushboolean(state, match);
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 is_device(lua_State* state)
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
    static const struct {
        const char* name;
        int32       (*method)(lua_State*);
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
        { "fnmatch",       &api_fnmatch },
        // UNDOCUMENTED; internal use only.
        { "isdevice",      &is_device },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, 0, sizeof_array(methods));

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "path");
}
