// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "version.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>

#include <unordered_set>



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
extern int              lua_execute(lua_State* state);

//------------------------------------------------------------------------------
static int to_lowercase(lua_State* state)
{
    const char* string;
    char* lowered;
    int length;
    int i;

    // Check we've got at least one argument...
    if (lua_gettop(state) == 0)
    {
        return 0;
    }

    // ...and that the argument is a string.
    if (!lua_isstring(state, 1))
    {
        return 0;
    }

    string = lua_tostring(state, 1);
    length = (int)strlen(string);

    lowered = (char*)malloc(length + 1);
    if (_rl_completion_case_map)
    {
        for (i = 0; i <= length; ++i)
        {
            char c = string[i];
            if (c == '-')
            {
                c = '_';
            }
            else
            {
                c = tolower(c);
            }

            lowered[i] = c;
        }
    }
    else
    {
        for (i = 0; i <= length; ++i)
        {
            char c = string[i];
            lowered[i] = tolower(c);
        }
    }

    lua_pushstring(state, lowered);
    free(lowered);

    return 1;
}

//------------------------------------------------------------------------------
#if 0
static int matches_are_files(lua_State* state)
{
    int i = 1;

    if (lua_gettop(state) > 0)
        i = (int)lua_tointeger(state, 1);

#if MODE4
    rl_filename_completion_desired = i;
#endif
    return 0;
}
#endif

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

#if MODE4
    const char* string = lua_tostring(state, 1);
    const char* rl_cvar = rl_variable_value(string);
    if (rl_cvar == nullptr)
        return 0;

    lua_pushstring(state, rl_cvar);
    return 1;
#else
    return 0;
#endif // MODE4
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

//------------------------------------------------------------------------------
/// -name:  clink.register_match_generator
/// -arg:   func:function
/// -arg:   priority:integer
/// -show:  -- Deprecated form:
/// -show:  local function match_generator_func(text, first, last, match_builder)
/// -show:  &nbsp; -- `text` is the line text.
/// -show:  &nbsp; -- `first` is the index of the beginning of the end word.
/// -show:  &nbsp; -- `last` is the index of the end of the end word.
/// -show:  &nbsp; -- `clink.add_match()` is used to add matches.
/// -show:  &nbsp; -- return true if handled, or false to let another generator try.
/// -show:  end
/// -show:  clink.register_match_generator(match_generator_func, 10)<br/>
/// -show:  -- Replace with new form:
/// -show:  local g = clink.generator(10)
/// -show:  function g:generate(line_state, match_builder)
/// -show:  &nbsp; -- `line_state` is a <a href="#line">line</a> object.
/// -show:  &nbsp; -- `match_builder:<a href="#builder:addmatch">addmatch</a>()` is used to add matches.
/// -show:  &nbsp; -- return true if handled, or false to let another generator try.
/// -show:  end
/// -deprecated: clink.generator
/// Registers a generator function for producing matches.  The Clink schema has
/// changed significantly enough that match generators must be rewritten to use
/// the new API.  Generators are called at a different time than before, and are
/// given access to a different set of information than before.<br/>
/// <br/>
/// <strong>Calling this produces an error message and does not register the
/// match generator.  Scripts must be updated to use the new match generator API
/// instead.</strong>
static int register_match_generator(lua_State* state)
{
    lua_Debug ar = {};
    lua_getstack(state, 1, &ar);
    lua_getinfo(state, "Sl", &ar);
    const char* source = ar.source ? ar.source : "?";
    int line = ar.currentline;

    struct auto_str : public no_copy
    {
        auto_str(const char* p) { s = (char*)malloc(strlen(p) + 1); strcpy(s, p); }
        auto_str(auto_str&& a) { s = a.s; a.s = nullptr; }
        ~auto_str() { free(s); }
        auto_str& operator=(auto_str&& a) { s = a.s; a.s = nullptr; }

        char* s;
    };
    struct already_reported
    {
        already_reported(const char* s, int l) : source(s), line(l) {}
        auto_str source;
        int line;
    };
    static std::vector<already_reported> s_already;

    for (auto const& it : s_already)
    {
        if (strcmp(it.source.s, source) == 0 && it.line == line)
            return 1;
    }

    s_already.push_back(std::move(already_reported(source, line)));

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    logger::error(source, line, "clink.register_match_generator() is obsolete; script must be updated to use clink.generator() instead.");

    printf("Error: %s at line %u must be updated to use clink.generator() instead of clink.register_match_generator().\n", source, line);

    return 1;
}


// END -- Clink 0.4.8 API compatibility ----------------------------------------



//------------------------------------------------------------------------------
extern int set_current_dir(lua_State* state);
extern int glob_dirs(lua_State* state);
extern int glob_files(lua_State* state);
extern int get_aliases(lua_State* state);
extern int get_current_dir(lua_State* state);
extern int get_env(lua_State* state);
extern int get_env_names(lua_State* state);
extern int get_screen_info(lua_State* state);
extern int is_dir(lua_State* state);

//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        // Clink 0.4.8 API compatibility.  Clink 1.0.0a1 moved these APIs away
        // from "clink.", but backward compatibility requires them here as well.
        { "chdir",                  &set_current_dir },
        { "execute",                &lua_execute },
        { "find_dirs",              &glob_dirs },
        { "find_files",             &glob_files },
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
        { "lower",                  &to_lowercase },
#if 0
        { "matches_are_files",      &matches_are_files },
#endif
        { "register_match_generator", &register_match_generator },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pushinteger(state, CLINK_VERSION_MAJOR);
    lua_setfield(state, -2, "version_major");
    lua_pushinteger(state, CLINK_VERSION_MINOR);
    lua_setfield(state, -2, "version_minor");
    lua_pushinteger(state, CLINK_VERSION_PATCH);
    lua_setfield(state, -2, "version_patch");
    lua_pushstring(state, CLINK_COMMIT);
    lua_setfield(state, -2, "version_commit");

    lua_setglobal(state, "clink");
}
