// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "../../app/src/version.h" // Ugh.

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_transform.h>
#include <readline/readline.h>

#include <unordered_set>



//------------------------------------------------------------------------------
// Implemented in host.cpp.
// UNDOCUMENTED, because it's really only useful inside debugger.lua.
//  / -name:  clink.print
//  / -arg:   text:string
//  / -show:  clink.print("\x1b[32mgreen\x1b[m \x1b[35mmagenta\x1b[m")
//  / This is similar to <code>print()</code>, but this supports ANSI escape
//  / codes.

//------------------------------------------------------------------------------
/// -name:  clink.version_encoded
/// -var:   integer
/// The Clink version number encoded as a single integer following the format
/// <span class="arg">Mmmmpppp</span> where <span class="arg">M</span> is the
/// major part, <span class="arg">m</span> is the minor part, and
/// <span class="arg">p</span> is the patch part of the version number.
///
/// For example, Clink v95.6.723 would be <code>950060723</code>.
///
/// This format makes it easy to test for feature availability by encoding
/// version numbers from the release notes.

//------------------------------------------------------------------------------
/// -name:  clink.version_major
/// -var:   integer
/// The major part of the Clink version number.
/// For v<strong>1</strong>.2.3.a0f14d the major version is 1.

//------------------------------------------------------------------------------
/// -name:  clink.version_minor
/// -var:   integer
/// The minor part of the Clink version number.
/// For v1.<strong>2</strong>.3.a0f14d the minor version is 2.

//------------------------------------------------------------------------------
/// -name:  clink.version_patch
/// -var:   integer
/// The patch part of the Clink version number.
/// For v1.2.<strong>3</strong>.a0f14d the patch version is 3.

//------------------------------------------------------------------------------
/// -name:  clink.version_commit
/// -var:   string
/// The commit part of the Clink version number.
/// For v1.2.3.<strong>a0f14d</strong> the commit part is a0f14d.



// BEGIN -- Clink 0.4.8 API compatibility --------------------------------------

extern "C" {
#include "lua.h"
extern int              _rl_completion_case_map;
extern const char*      rl_readline_name;
}

extern int              get_clink_setting(lua_State* state);
extern int              glob_impl(lua_State* state, bool dirs_only, bool back_compat);
extern int              lua_execute(lua_State* state);

//------------------------------------------------------------------------------
int old_glob_dirs(lua_State* state)
{
    return glob_impl(state, true, true/*back_compat*/);
}

//------------------------------------------------------------------------------
int old_glob_files(lua_State* state)
{
    return glob_impl(state, false, true/*back_compat*/);
}

//------------------------------------------------------------------------------
static int get_setting_str(lua_State* state)
{
    return get_clink_setting(state);
}

//------------------------------------------------------------------------------
static int get_setting_int(lua_State* state)
{
    return get_clink_setting(state);
}

//------------------------------------------------------------------------------
static int get_rl_variable(lua_State* state)
{
    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
        return 0;

    const char* string = lua_tostring(state, 1);
    const char* rl_cvar = rl_variable_value(string);
    if (rl_cvar == nullptr)
        return 0;

    lua_pushstring(state, rl_cvar);
    return 1;
}

//------------------------------------------------------------------------------
static int is_rl_variable_true(lua_State* state)
{
    int i;
    const char* cvar_value;

    i = get_rl_variable(state);
    if (i == 0)
    {
        return 0;
    }

    cvar_value = lua_tostring(state, -1);
    i = (_stricmp(cvar_value, "on") == 0) || (_stricmp(cvar_value, "1") == 0);
    lua_pop(state, 1);
    lua_pushboolean(state, i);

    return 1;
}

//------------------------------------------------------------------------------
static int get_host_process(lua_State* state)
{
    lua_pushstring(state, rl_readline_name);
    return 1;
}



// END -- Clink 0.4.8 API compatibility ----------------------------------------



//------------------------------------------------------------------------------
/// -name:  clink.match_display_filter
/// -var:   function
/// -deprecated: builder:addmatch
/// -show:  clink.match_display_filter = function(matches)
/// -show:  &nbsp; -- Transform matches.
/// -show:  &nbsp; return matches
/// -show:  end
/// This is no longer used.

//------------------------------------------------------------------------------
static int map_string(lua_State* state, transform_mode mode)
{
    const char* string;
    int length;

    // Check we've got at least one argument...
    if (lua_gettop(state) == 0)
        return 0;

    // ...and that the argument is a string.
    if (!lua_isstring(state, 1))
        return 0;

    string = lua_tostring(state, 1);
    length = (int)strlen(string);

    wstr<> out;
    if (length)
    {
        wstr<> in(string);
        str_transform(in.c_str(), in.length(), out, mode);
    }

    if (_rl_completion_case_map)
    {
        for (unsigned int i = 0; i < out.length(); ++i)
        {
            if (out[i] == '-' && (mode != transform_mode::upper))
                out.data()[i] = '_';
            else if (out[i] == '_' && (mode == transform_mode::upper))
                out.data()[i] = '-';
        }
    }

    str<> text(out.c_str());

    lua_pushlstring(state, text.c_str(), text.length());

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.lower
/// -arg:   text:string
/// -ret:   string
/// -show:  clink.lower("Hello World") -- returns "hello world"
/// This API correctly converts UTF8 strings to lowercase, with international
/// linguistic awareness.
static int to_lowercase(lua_State* state)
{
    return map_string(state, transform_mode::lower);
}

//------------------------------------------------------------------------------
/// -name:  clink.upper
/// -arg:   text:string
/// -ret:   string
/// -show:  clink.upper("Hello World") -- returns "HELLO WORLD"
/// This API correctly converts UTF8 strings to uppercase, with international
/// linguistic awareness.
static int to_uppercase(lua_State* state)
{
    return map_string(state, transform_mode::upper);
}

//------------------------------------------------------------------------------
/// -name:  clink.getsession
/// -ret:   string
/// -show:  local c = os.getalias("clink")
/// -show:  local r = io.popen(c.." --session "..clink.getsession().." history")
/// Returns the current Clink session id.
///
/// This is needed when using <code>io.popen()</code> (or similar functions) to
/// invoke <code>clink history</code> or <code>clink info</code> while Clink is
/// installed for autorun.  The popen API spawns a new CMD.exe, which gets a new
/// Clink instance injected, so the history or info command will use the new
/// session unless explicitly directed to use the calling session.
static int get_session(lua_State* state)
{
    str<32> session;
    session.format("%d", GetCurrentProcessId());
    lua_pushlstring(state, session.c_str(), session.length());
    return 1;
}



//------------------------------------------------------------------------------
extern int set_current_dir(lua_State* state);
extern int get_aliases(lua_State* state);
extern int get_current_dir(lua_State* state);
extern int get_env(lua_State* state);
extern int get_env_names(lua_State* state);
extern int get_screen_info(lua_State* state);
extern int is_dir(lua_State* state);
extern int clink_print(lua_State* state);

//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        // APIs in the "clink." namespace.
        { "lower",                  &to_lowercase },
        { "print",                  &clink_print },
        { "upper",                  &to_uppercase },
        { "getsession",             &get_session },
        // Backward compatibility with the Clink 0.4.8 API.  Clink 1.0.0a1 had
        // moved these APIs away from "clink.", but backward compatibility
        // requires them here as well.
        { "chdir",                  &set_current_dir },
        { "execute",                &lua_execute },
        { "find_dirs",              &old_glob_dirs },
        { "find_files",             &old_glob_files },
        { "get_console_aliases",    &get_aliases },
        { "get_cwd",                &get_current_dir },
        { "get_env",                &get_env },
        { "get_env_var_names",      &get_env_names },
        { "get_host_process",       &get_host_process },
        { "get_rl_variable",        &get_rl_variable },
        { "get_screen_info",        &get_screen_info },
        { "get_setting_int",        &get_setting_int },
        { "get_setting_str",        &get_setting_str },
        { "is_dir",                 &is_dir },
        { "is_rl_variable_true",    &is_rl_variable_true },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pushinteger(state, CLINK_VERSION_MAJOR * 10000000 +
                           CLINK_VERSION_MINOR *    10000 +
                           CLINK_VERSION_PATCH);
    lua_setfield(state, -2, "version_encoded");
    lua_pushinteger(state, CLINK_VERSION_MAJOR);
    lua_setfield(state, -2, "version_major");
    lua_pushinteger(state, CLINK_VERSION_MINOR);
    lua_setfield(state, -2, "version_minor");
    lua_pushinteger(state, CLINK_VERSION_PATCH);
    lua_setfield(state, -2, "version_patch");
    lua_pushstring(state, AS_STR(CLINK_COMMIT));
    lua_setfield(state, -2, "version_commit");

    lua_setglobal(state, "clink");
}
