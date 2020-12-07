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
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
/// -name:  path.collapsetilde
/// -arg:   path:string
/// -arg:   [force:boolean]
/// -ret:   string
/// -show:  path.collapsetilde("C:\Users\yourusername\Documents")
/// -show:
/// -show:  -- The return value depends on the expand-tilde configuration variable:
/// -show:  -- When "on", the function returns "C:\Users\yourusername\Documents".
/// -show:  -- When "off", the function returns "~\Documents".
/// -show:
/// -show:  -- Or when <em>force</em> is true, the function returns "~\Documents".
/// Undoes Readline tilde expansion.  See <a href="#expandtilde">expandtilde</a>
/// for more information.
static int collapse_tilde(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    int force = lua_toboolean(state, 2);

    const char* rl_cvar = rl_variable_value("expand-tilde");
    bool expand_tilde = (rl_cvar && rl_cvar[0] == 'o' && rl_cvar[1] == 'n' && rl_cvar[2] == '\0');

    if (!expand_tilde || force)
    {
        char* tilde = tilde_expand("~");
        if (tilde)
        {
            int tilde_len = int(strlen(tilde));
            int j = str_compare(path, tilde);
            free(tilde);

            if (j < 0 || j == tilde_len)
            {
                str<> collapsed;
                collapsed.format("~%s", path + tilde_len);
                lua_pushstring(state, collapsed.c_str());
                return 1;
            }
        }
    }

    lua_pushstring(state, path);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  path.expandtilde
/// -arg:   path:string
/// -ret:   string, boolean
/// -show:  path.expandtilde("~\Documents") -- returns "C:\Users\yourusername\Documents", true
/// -show:
/// -show:  -- This demonstrates efficient use of rl.expandtilde() and rl.collapsetilde()
/// -show:  -- to generate directory matches from the file system.
/// -show:  function dir_matches(match_word, word_index, line_state)
/// -show:  &nbsp; -- Expand tilde before scanning file system.
/// -show:  &nbsp; local word = line_state:getword(word_index)
/// -show:  &nbsp; local expanded
/// -show:  &nbsp; word, expanded = rl.expandtilde(word)
/// -show:
/// -show:  &nbsp; -- Get the directory from 'word', and collapse tilde before generating
/// -show:  &nbsp; -- matches.  Notice that collapsetilde() only needs to be called once!
/// -show:  &nbsp; local root = path.getdirectory(word) or ""
/// -show:  &nbsp; if expanded then
/// -show:  &nbsp; &nbsp; root = rl.collapsetilde(root)
/// -show:  &nbsp; end
/// -show:
/// -show:  &nbsp; local matches = {}
/// -show:  &nbsp; for _, d in ipairs(os.globdirs(word.."*")) do
/// -show:  &nbsp;   -- Join the filename with the input directory (might have a tilde).
/// -show:  &nbsp;   local dir = path.join(root, d)
/// -show:  &nbsp;   if os.ishidden(dir) then
/// -show:  &nbsp;     table.insert(matches, { match = dir, type = "dir,hidden" })
/// -show:  &nbsp;   else
/// -show:  &nbsp;     table.insert(matches, { match = dir, type = "dir" })
/// -show:  &nbsp;   end
/// -show:  &nbsp; end
/// -show:  &nbsp; return matches
/// -show:  end
/// Performs Readline tilde expansion.<br/>
/// <br/>
/// When generating filename matches for a word, use the
/// <code>rl.expandtilde()</code> and <code>rl.collapsetilde()</code> helpers to
/// perform tilde expansion according to Readline rules/settings.<br/>
/// <br/>
/// Use <code>rl.expandtilde()</code> to do tilde expansion before using
/// collecting file matches (e.g. via <code>os.<a href="#globfiles">globfiles()</a></code>).
/// If expandetilde returns that it expanded the string, then use
/// <code>rl.<a href="#collapsetilde">collapsetilde</a>(match)</code> to put
/// back the tilde before returning the match.  You can use
/// <code>rl.<a href="#isvariabletrue">isvariabletrue</a>("expand-tilde")</code>
/// to test whether matches should include the expanded tilde, but
/// <code>rl.collapsetilde()</code> automatically tests that config var unless
/// <code>true</code> is passed to force collapsing.
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
/// -name:  getvariable
/// -arg:   name:string
/// -ret:   string|nil
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
/// -name:  isvariabletrue
/// -arg:   name:string
/// -ret:   boolean|nil
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
