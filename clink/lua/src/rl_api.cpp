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
#include "lib/matches.h"
#include "match_builder_lua.h"

extern "C" {
#include "lua.h"
extern int              _rl_completion_case_map;
extern const char*      rl_readline_name;
}

extern matches* get_mutable_matches(bool nosort=false);
extern const char* get_last_luafunc();
extern void override_rl_last_func(rl_command_func_t* func);



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
/// -name:  rl.invokecommand
/// -arg:   command:string
/// -arg:   [count:integer]
/// -ret:   boolean | nil
/// Invokes a Readline command named <span class="arg">command</span>.  May only
/// be used within a <code>luafunc:</code> key binding.
///
/// <span class="arg">count</span> is optional and defaults to 1 if omitted.
///
/// Returns true if the named command succeeds, false if the named command
/// fails, or nil if the named command doesn't exist.
///
/// Warning:  Invoking more than one Readline command in a <code>luafunc:</code>
/// key binding could have unexpected results, depending on which commands are
/// invoked.
static int invoke_command(lua_State* state)
{
    if (!lua_state::is_in_luafunc())
        return luaL_error(state, "rl.invokecommand may only be used in a 'luafunc:' key binding");

    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
        return 0;

    const char* command = lua_tostring(state, 1);
    rl_command_func_t *func = rl_named_function(command);
    if (func == nullptr)
        return 0;

    int isnum;
    int count = int(lua_tointegerx(state, 2, &isnum));
    int err = func(isnum ? count : 1, 0/*invoking_key*/);

    override_rl_last_func(func);

    lua_pushinteger(state, err);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.getlastcommand
/// -ret:   string, function
/// -show:  local last_rl_func, last_lua_func = rl.getlastcommand()
/// Returns two values:
/// <ul>
/// <li>The name of the last Readline command invoked by a key binding.
/// <li>The name of the last Lua function invoked by a key binding.
/// </ul>
///
/// If the last key binding invoked a Lua function, then the first return value
/// is an empty string unless the Lua function used
/// <a href="#rl.invokecommand">rl.invokecommand()</a> to also internally invoke
/// a Readline command.
/// If the last key binding did not invoke a Lua function, then the second
/// return value is an empty string.
static int get_last_command(lua_State* state)
{
    const char* last_rl_func_name = "";
    if (rl_last_func)
    {
        for (const FUNMAP* const* walk = funmap; *walk; ++walk)
        {
            if ((*walk)->function == rl_last_func)
            {
                last_rl_func_name = (*walk)->name;
                break;
            }
        }
    }

    lua_pushstring(state, last_rl_func_name);
    lua_pushstring(state, get_last_luafunc());
    return 2;
}

//------------------------------------------------------------------------------
/// -name:  rl.setmatches
/// -arg:   matches:table
/// -arg:   [type:string]
/// -ret:   integer, boolean
/// Provides an alternative set of matches for the current word.  This discards
/// any matches that may have already been collected and uses
/// <span class="arg">matches</span> for subsequent Readline completion commands
/// until any action that normally resets the matches (such as moving the cursor
/// or editing the input line).
///
/// May only be used within a <code>luafunc:</code> key binding.
///
/// The syntax is the same as for
/// <a href="#builder:addmatches()">builder:addmatches()</a> with one addition:
/// You can add a <code>"nosort"</code> key to the
/// <span class="arg">matches</span> table to disable sorting the matches.
///
/// <pre><code class="lua">local matches = {}<br/>matches["nosort"] = true<br/>rl.setmatches(matches)</code></pre>
///
/// This function can be used by a <code>luafunc:</code> key binding to provide
/// matches based on some special criteria.  For example, a key binding could
/// collect numbers from the current screen buffer (such as issue numbers,
/// commit hashes, line numbers, etc) and provide them to Readline as matches,
/// making it convenient to grab a number from the screen and insert it as a
/// command line argument.
///
/// Match display filtering is also possible by using
/// <a href="#clink.ondisplaymatches">clink.ondisplaymatches()</a> after setting
/// the matches.
///
/// <em>Example .inputrc key binding:</em>
/// <pre><code class="plaintext">M-n:            <span class="hljs-string">"luafunc:completenumbers"</span>       <span class="hljs-comment"># Alt+N</span></code></pre>
///
/// <em>Example Lua function:</em>
/// -show:  function completenumbers()
/// -show:  &nbsp; local _,last_luafunc = rl.getlastcommand()
/// -show:  &nbsp; if last_luafunc ~= "completenumbers" then
/// -show:  &nbsp;   -- Collect numbers from the screen (minimum of three digits).
/// -show:  &nbsp;   -- The numbers can be any base up to hexadecimal (decimal, octal, etc).
/// -show:  &nbsp;   local matches = console.screengrab("[^%w]*(%w%w[%w]+)", "^%x+$")
/// -show:  &nbsp;   -- They're already sorted by distance from the input line.
/// -show:  &nbsp;   matches["nosort"] = true
/// -show:  &nbsp;   rl.setmatches(matches)
/// -show:  &nbsp; end
/// -show:
/// -show:  &nbsp; rl.invokecommand("old-menu-complete")
/// -show:  end
static int set_matches(lua_State* state)
{
    bool nosort = false;
    if (lua_istable(state, 1))
    {
        lua_getfield(state, 1, "nosort");
        nosort = !lua_isnil(state, -1);
        lua_pop(state, 1);
    }

    matches* matches = get_mutable_matches(nosort);
    if (!matches)
        return 0;

    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_reset_display_filter");
    lua_rawget(state, -2);
    if (lua_state::pcall(state, 0, 0) != 0)
    {
        puts(lua_tostring(state, -1));
        return 0;
    }

    rl_last_func = nullptr;

    match_builder builder(*matches);
    match_builder_lua builder_lua(builder);

    return builder_lua.add_matches(state);
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
        { "invokecommand",          &invoke_command },
        { "getlastcommand",         &get_last_command },
        { "setmatches",             &set_matches },
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
