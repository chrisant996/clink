// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_iter.h>
#include <readline/readline.h>

extern "C" {
#include "lua.h"
extern int              _rl_completion_case_map;
extern const char*      rl_readline_name;
}



//------------------------------------------------------------------------------
bool collapse_tilde(const char* in, str_base& out, bool force)
{
    const char* rl_cvar = rl_variable_value("expand-tilde");
    bool expand_tilde = (rl_cvar && rl_cvar[0] == 'o' && rl_cvar[1] == 'n' && rl_cvar[2] == '\0');
    if (expand_tilde && !force)
        return false;

    char *tilde = tilde_expand("~");
    if (!tilde)
        return false;

    int tilde_len = int(strlen(tilde));
    int j = str_compare(in, tilde);
    free(tilde);

    if (j >= 0 && j != tilde_len)
        return true;

    out.format("~%s", in + tilde_len);
    return true;
}



//------------------------------------------------------------------------------
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
/// -name:  rl.collapsetilde
/// -arg:   path:string
/// -arg:   [force:boolean]
/// -ret:   string
/// -show:  rl.collapsetilde("C:\Users\yourusername\Documents")
/// -show:  &nbsp;
/// -show:  -- The return value depends on the expand-tilde configuration variable:
/// -show:  -- When "on", the function returns "C:\Users\yourusername\Documents".
/// -show:  -- When "off", the function returns "~\Documents".
/// -show:  &nbsp;
/// -show:  -- Or when <span class="arg">force</span> is true, the function returns "~\Documents".
/// Undoes Readline tilde expansion.  See <a href="#rl.expandtilde">rl.expandtilde</a>
/// for more information.
static int collapse_tilde(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    int force = lua_toboolean(state, 2);

    const char* rl_cvar = rl_variable_value("expand-tilde");
    bool expand_tilde = (rl_cvar && rl_cvar[0] == 'o' && rl_cvar[1] == 'n' && rl_cvar[2] == '\0');

    str<> collapsed;
    if (collapse_tilde(path, collapsed, false))
        lua_pushlstring(state, collapsed.c_str(), collapsed.length());
    else
        lua_pushstring(state, path);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.expandtilde
/// -arg:   path:string
/// -ret:   string, boolean
/// -show:  local result, expanded = rl.expandtilde("~\Documents")
/// -show:  -- result is "C:\Users\yourusername\Documents"
/// -show:  -- expanded is true
/// -show:  &nbsp;
/// -show:  -- This dir_matches function demonstrates efficient use of rl.expandtilde()
/// -show:  -- and rl.collapsetilde() to generate directory matches from the file system.
/// -show:  function dir_matches(match_word, word_index, line_state)
/// -show:  &nbsp; -- Expand tilde before scanning file system.
/// -show:  &nbsp; local word = line_state:getword(word_index)
/// -show:  &nbsp; local expanded
/// -show:  &nbsp; word, expanded = rl.expandtilde(word)
/// -show:  &nbsp;
/// -show:  &nbsp; -- Get the directory from 'word', and collapse tilde before generating
/// -show:  &nbsp; -- matches.  Notice that collapsetilde() only needs to be called once!
/// -show:  &nbsp; local root = path.getdirectory(word) or ""
/// -show:  &nbsp; if expanded then
/// -show:  &nbsp; &nbsp; root = rl.collapsetilde(root)
/// -show:  &nbsp; end
/// -show:  &nbsp;
/// -show:  &nbsp; local matches = {}
/// -show:  &nbsp; for _, d in ipairs(os.globdirs(word.."*", true)) do
/// -show:  &nbsp;   -- Join the filename with the input directory (might have a tilde).
/// -show:  &nbsp;   local dir = path.join(root, d.name)
/// -show:  &nbsp;   table.insert(matches, { match = dir, type = d.type })
/// -show:  &nbsp; end
/// -show:  &nbsp; return matches
/// -show:  end
/// Performs Readline tilde expansion.
///
/// When generating filename matches for a word, use the 
/// <a href="#rl.expandtilde">rl.expandtilde</a> and
/// <a href="#rl.collapsetilde">rl.collapsetilde</a> helper functions to perform
/// tilde completion expansion according to Readline's configuration.
///
/// Use <a href="#rl.expandtilde">rl.expandtilde</a> to do tilde expansion 
/// before collecting file matches (e.g. via
/// <a href="#os.globfiles">os.globfiles</a>).  If it indicates that it expanded
/// the string, then use <a href="#rl.collapsetilde">rl.collapsetilde</a> to put
/// back the tilde before returning a match.
static int expand_tilde(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    char* expanded_path = tilde_expand(path);
    bool expanded = expanded_path && strcmp(path, expanded_path) != 0;
    lua_pushstring(state, expanded_path ? expanded_path : path);
    lua_pushboolean(state, expanded);
    free(expanded_path);
    return 2;
}

//------------------------------------------------------------------------------
/// -name:  rl.getvariable
/// -arg:   name:string
/// -ret:   string | nil
/// Returns the value of the named Readline configuration variable as a string,
/// or nil if the variable name is not recognized.
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
/// -name:  rl.isvariabletrue
/// -arg:   name:string
/// -ret:   boolean | nil
/// Returns a boolean value indicating whether the named Readline configuration
/// variable is set to true (on), or nil if the variable name is not recognized.
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
void rl_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "collapsetilde",          &collapse_tilde },
        { "expandtilde",            &expand_tilde },
        { "getvariable",            &get_rl_variable },
        { "isvariabletrue",         &is_rl_variable_true },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "rl");
}
