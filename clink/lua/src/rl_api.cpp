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
#include <terminal/ecma48_iter.h>
#include <lib/host_callbacks.h>
#include <lib/line_editor_integration.h>
#include <lib/rl_integration.h>
#include <lib/matches.h>
#include <lib/match_colors.h>
#include <lib/display_readline.h>
#include "match_builder_lua.h"
#include "prompt.h"

#include <vector>

extern "C" {
#include <lua.h>
#include <lstate.h>
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
}

// TODO: Clean up extern.
extern int32 count_prompt_lines(const char* prompt_prefix);



//------------------------------------------------------------------------------
bool collapse_tilde(const char* in, str_base& out, bool force)
{
    const char* rl_cvar = rl_variable_value("expand-tilde");
    bool expand_tilde = (rl_cvar && rl_cvar[0] == 'o' && rl_cvar[1] == 'n' && rl_cvar[2] == '\0');
    if (expand_tilde && !force)
        return false;

    str_moveable tilde;
    if (!path::tilde_expand("~", tilde))
        return false;
    path::maybe_strip_last_separator(tilde);

    str_iter in_iter(in);
    str_iter tilde_iter(tilde.c_str());
    {
        str_compare_scope _(str_compare_scope::caseless, false);
        int32 j = str_compare(in_iter, tilde_iter);
        if (j >= 0 && tilde_iter.more())
            return false;
    }

    out.format("~%s", in_iter.get_pointer());
    return true;
}

//------------------------------------------------------------------------------
static void unquote_keys(const char* in, str_base& out)
{
    out.clear();
    if (in && in[0] == '"')
    {
        out.concat(in + 1);
        if (out.length() && out.c_str()[out.length() - 1] == in[0])
            out.truncate(out.length() - 1);
    }
    else
    {
        out.concat(in);
    }
}



//------------------------------------------------------------------------------
/// -name:  rl.collapsetilde
/// -ver:   1.1.6
/// -arg:   path:string
/// -arg:   [force:boolean]
/// -ret:   string
/// Undoes Readline tilde expansion.  See
/// <a href="#rl.expandtilde">rl.expandtilde()</a> for more information.
/// -show:  rl.collapsetilde("C:\\Users\\yourusername\\Documents")
/// -show:  &nbsp;
/// -show:  -- The return value depends on the expand-tilde configuration variable:
/// -show:  -- When "on", the function returns "C:\\Users\\yourusername\\Documents".
/// -show:  -- When "off", the function returns "~\\Documents".
/// -show:  &nbsp;
/// -show:  -- Or when <span class="arg">force</span> is true, the function returns "~\Documents".
static int32 collapse_tilde(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    int32 force = lua_toboolean(state, 2);

    str<> collapsed;
    if (collapse_tilde(path, collapsed, !!force))
        lua_pushlstring(state, collapsed.c_str(), collapsed.length());
    else
        lua_pushstring(state, path);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.expandtilde
/// -ver:   1.1.6
/// -arg:   path:string
/// -arg:   [whole_line:boolean]
/// -ret:   string, boolean
/// Performs Readline tilde expansion.
///
/// When generating filename matches for a word, use the
/// <a href="#rl.expandtilde">rl.expandtilde()</a> and
/// <a href="#rl.collapsetilde">rl.collapsetilde()</a> helper functions to perform
/// tilde completion expansion according to Readline's configuration.
///
/// An optional <span class="arg">whole_line</span> argument selects whether to
/// expand tildes everywhere in the input string (pass true), or to expand only
/// a tilde at the beginning of the input string (pass false or omit the
/// second argument).  See the Compatibility Note below for more information.
///
/// Use <a href="#rl.expandtilde">rl.expandtilde()</a> to do tilde expansion
/// before collecting file matches (e.g. via
/// <a href="#os.globfiles">os.globfiles()</a>).  If it indicates that it expanded
/// the string, then use <a href="#rl.collapsetilde">rl.collapsetilde()</a> to put
/// back the tilde before returning a match.
/// -show:  local result, expanded = rl.expandtilde("~\\Documents")
/// -show:  -- result is "C:\\Users\\yourusername\\Documents"
/// -show:  -- expanded is true
/// -show:
/// -show:  -- This dir_matches function demonstrates efficient use of rl.expandtilde()
/// -show:  -- and rl.collapsetilde() to generate directory matches from the file system.
/// -show:  function dir_matches(match_word, word_index, line_state)
/// -show:  &nbsp;   -- Expand tilde before scanning file system.
/// -show:  &nbsp;   local word = line_state:getword(word_index)
/// -show:  &nbsp;   local expanded
/// -show:  &nbsp;   word, expanded = rl.expandtilde(word)
/// -show:
/// -show:  &nbsp;   -- Get the directory from 'word', and collapse tilde before generating
/// -show:  &nbsp;   -- matches.  Notice that collapsetilde() only needs to be called once!
/// -show:  &nbsp;   local root = path.getdirectory(word) or ""
/// -show:  &nbsp;   if expanded then
/// -show:  &nbsp;       root = rl.collapsetilde(root)
/// -show:  &nbsp;   end
/// -show:
/// -show:  &nbsp;   local matches = {}
/// -show:  &nbsp;   for _, d in ipairs(os.globdirs(word.."*", true)) do
/// -show:  &nbsp;       -- Join the filename with the input directory (might have a tilde).
/// -show:  &nbsp;       local dir = path.join(root, d.name)
/// -show:  &nbsp;       table.insert(matches, { match = dir, type = d.type })
/// -show:  &nbsp;   end
/// -show:  &nbsp;   return matches
/// -show:  end
/// <fieldset><legend>Compatibility Note:</legend>
/// The original intended usage for this function was to expand tildes in a
/// single word.  But sometimes it may be convenient to expand tildes for an
/// entire command line all at once.
///
/// Prior to v1.3.36, this function simply asked the Readline library to expand
/// tildes, but that mode of operation doesn't respect quotes and has quirks
/// that can produce unexpected results when the input is a single pathname
/// (which was the documented supported usage).
///
/// In v1.3.36 this function fixed that problem, and expands tildes correctly
/// a single pathname as the input (and also accepts quotes).  But that broke an
/// undocumented quirk that could expand tildes for a whole command line as
/// the input.
///
/// In v1.3.37 and newer, this function accepts a boolean second argument which
/// selects whether to expand for a whole input line using the quirky Readline
/// tilde expansion (pass true), or to expand for a single pathname as the input
/// (pass false or omit the second argument).
///
/// Passing true for the second argument causes any version of Clink (except
/// v1.3.36) to expand the input as a whole command line.
/// </fieldset>
static int32 expand_tilde(lua_State* state)
{
    const char* in = checkstring(state, 1);
    bool whole_line = lua_toboolean(state, 2);
    if (!in)
        return 0;

    if (whole_line)
    {
        char* expanded_path = tilde_expand(in);
        bool expanded = expanded_path && strcmp(in, expanded_path) != 0;
        lua_pushstring(state, expanded_path ? expanded_path : in);
        lua_pushboolean(state, expanded);
        free(expanded_path);
        return 2;
    }
    else
    {
        // Strip all quotes.
        const bool beginquote = (in[0] == '"');
        bool endquote = false;
        str<> tmp;
        for (const char* walk = in; *walk; ++walk)
        {
            if (*walk != '"')
                tmp.concat(walk, 1);
            else if (!walk[1])
                endquote = true;
        }

        // Expand the input as a single word.
        str<> expanded_path;
        bool expanded = tmp.c_str()[0] == '~' && path::tilde_expand(tmp.c_str(), expanded_path);

        if (expanded)
        {
            // Avoid adding \ at the end if it's just `~` by itself, to avoid
            // running afoul of argv quoting rules for `\"`.
            if (tmp.c_str()[0] == '~' && tmp.length() == 1)
            {
                while (expanded_path.length() && expanded_path.c_str()[expanded_path.length() - 1] == '\\')
                    expanded_path.truncate(expanded_path.length() - 1);
            }

            // Add quotes again if the input was originally quoted.
            str<> out;
            if (beginquote)
                out << "\"";
            out << expanded_path.c_str();
            if (endquote)
                out << "\"";
            lua_pushstring(state, out.c_str());
        }
        else
        {
            lua_pushstring(state, in);
        }

        lua_pushboolean(state, expanded);
        return 2;
    }
}

//------------------------------------------------------------------------------
/// -name:  rl.getvariable
/// -ver:   1.1.6
/// -arg:   name:string
/// -ret:   string | nil
/// Returns the value of the named Readline configuration variable as a string,
/// or nil if the variable name is not recognized.
static int32 get_rl_variable(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (!name)
        return 0;

    const char* rl_cvar = rl_variable_value(name);
    if (rl_cvar == nullptr)
        return 0;

    lua_pushstring(state, rl_cvar);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.setvariable
/// -ver:   1.1.46
/// -arg:   name:string
/// -arg:   value:string
/// -ret:   boolean
/// Temporarily overrides the named Readline configuration variable to the
/// specified value.  The return value reports whether it was successful, or is
/// nil if the variable name is not recognized.
///
/// <strong>Note:</strong> This does not write the value into a config file.
/// Instead it updates the variable in memory, temporarily overriding whatever
/// is present in any config files.  When config files are reloaded, they may
/// replace the value again.
static int32 set_rl_variable(lua_State* state)
{
    const char* name = checkstring(state, 1);
    const char* value = checkstring(state, 2);
    if (!name || !value)
        return 0;

    const char* rl_cvar = rl_variable_value(name);
    if (rl_cvar == nullptr)
        return 0;

    int32 failed = rl_variable_bind(name, value);
    lua_pushboolean(state, !failed);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.isvariabletrue
/// -ver:   1.1.6
/// -arg:   name:string
/// -ret:   boolean | nil
/// Returns a boolean value indicating whether the named Readline configuration
/// variable is set to true (on), or nil if the variable name is not recognized.
static int32 is_rl_variable_true(lua_State* state)
{
    if (!get_rl_variable(state))
        return 0;

    const char* cvar_value = checkstring(state, -1);
    if (!cvar_value)
        return 0;

    bool on = (_stricmp(cvar_value, "on") == 0) || (_stricmp(cvar_value, "1") == 0);
    lua_pushboolean(state, on);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.getbinding
/// -ver:   1.2.46
/// -arg:   key:string
/// -arg:   [keymap:string]
/// -ret:   binding:string, type:string
/// Returns the command or macro bound to <span class="arg">key</span>, and the
/// type of the binding.
///
/// If nothing is bound to the specified key sequence, the returned binding will
/// be nil.
///
/// The returned type can be <code>"function"</code>, <code>"macro"</code>, or
/// <code>"keymap"</code> (if <span class="arg">key</span> is an incomplete key
/// sequence).
///
/// If an error occurs, only nil is returned.
///
/// The <span class="arg">key</span> sequence string is the same format as from
/// <code>clink echo</code>.  See <a href="#discoverkeysequences">Discovering
/// Clink key bindings</a> for more information.
///
/// An optional <span class="arg">keymap</span> may be specified as well.  If it
/// is omitted or nil, then the current keymap is searched.  Otherwise it may
/// refer to one of the three built in keymaps:
/// <table>
/// <tr><th>Keymap</th><th>Description</th></tr>
/// <tr><td><code>"emacs"</code></td><td>The Emacs keymap, which is the default keymap.</td></tr>
/// <tr><td><code>"vi"</code>, <code>"vi-move"</code>, or <code>"vi-command"</code></td><td>The VI command mode keymap.</td></tr>
/// <tr><td><code>"vi-insert"</code></td><td>The VI insertion mode keymap.</td></tr>
/// </table>
///
/// The return value can be passed as input to
/// <a href="#rl.setbinding">rl.setbinding()</a> or
/// <a href="#rl.invokecommand">rl.invokecommand()</a>.
/// -show:  local b,t = rl.getbinding([["\e[H"]], "emacs")
/// -show:  if b then
/// -show:  &nbsp;   print("Home is bound to "..b.." ("..t..") in emacs mode.")
/// -show:  else
/// -show:  &nbsp;   print("Home is not bound in emacs mode.")
/// -show:  end
static int32 get_rl_binding(lua_State* state)
{
    if (!funmap)
        return 0;

    const char* _key = checkstring(state, 1);
    const char* keymap = optstring(state, 2, nullptr);
    if (!_key)
        return 0;

    Keymap map = keymap ? rl_get_keymap_by_name(keymap) : rl_get_keymap();

    int32 type;
    rl_command_func_t* func = nullptr;

    {
        str<> keys;
        unquote_keys(_key, keys);

        int32 keylen = 0;
        char* keyseq = static_cast<char*>(malloc(keys.length() * 2 + 1));
        if (rl_translate_keyseq(keys.c_str(), keyseq, &keylen))
        {
            free(keyseq);
            return 0;
        }

        func = rl_function_of_keyseq_len(keyseq, keylen, map, &type);
        free(keyseq);
    }

    if (func)
    {
        if (type == ISFUNC)
        {
            for (const FUNMAP* const* walk = funmap; *walk; ++walk)
            {
                if ((*walk)->function == func)
                {
                    lua_pushstring(state, (*walk)->name);
                    lua_pushliteral(state, "function");
                    return 2;
                }
            }
        }
        else if (type == ISKMAP)
        {
            // Bound to a keymap, i.e. the key sequence is incomplete.
            lua_pushnil(state);
            lua_pushliteral(state, "keymap");
            return 2;
        }
        else if (type == ISMACR)
        {
            str<> tmp;

            char* macro = _rl_untranslate_macro_value((char*)func, 0);
            if (macro)
                tmp << "\"" << macro << "\"";
            else
                tmp << "unknown macro";
            free(macro);

            lua_pushstring(state, tmp.c_str());
            lua_pushliteral(state, "macro");
            return 2;
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl.setbinding
/// -ver:   1.2.46
/// -arg:   key:string
/// -arg:   binding:string | nil
/// -arg:   [keymap:string]
/// -ret:   boolean
/// Binds <span class="arg">key</span> to invoke
/// <span class="arg">binding</span>, and returns whether it was successful.
///
/// The <span class="arg">key</span> sequence string is the same format as from
/// <code>clink echo</code>.  See <a href="#discoverkeysequences">Discovering
/// Clink key bindings</a> for more information.
///
/// The <span class="arg">binding</span> is either the name of a Readline
/// command, a quoted macro string (just like in the .inputrc config file), or
/// nil to clear the key binding.
///
/// An optional <span class="arg">keymap</span> may be specified as well.  If it
/// is omitted or nil, then the current keymap is searched.  Otherwise it may
/// refer to one of the three built in keymaps:
/// <table>
/// <tr><th>Keymap</th><th>Description</th></tr>
/// <tr><td><code>"emacs"</code></td><td>The Emacs keymap, which is the default keymap.</td></tr>
/// <tr><td><code>"vi"</code>, <code>"vi-move"</code>, or <code>"vi-command"</code></td><td>The VI command mode keymap.</td></tr>
/// <tr><td><code>"vi-insert"</code></td><td>The VI insertion mode keymap.</td></tr>
/// </table>
///
/// Using Lua's <code>[[</code>..<code>]]</code> string syntax conveniently lets
/// you simply copy the key string exactly from the <code>clink echo</code>
/// output, without needing to translate the quotes or backslashes.
///
/// <strong>Note:</strong> This does not write the value into a config file.
/// Instead it updates the key binding in memory, temporarily overriding
/// whatever is present in any config files.  When config files are reloaded,
/// they may replace the key binding again.
/// -show:  local old_space = rl.getbinding('" "')
/// -show:  function hijack_space(rl_buffer)
/// -show:  &nbsp;   rl.invokecommand("clink-expand-line")   -- Expand envvars, etc in the line.
/// -show:  &nbsp;   rl.invokecommand(old_space)             -- Then invoke whatever was previously bound to Space.
/// -show:  end
/// -show:  rl.setbinding([[" "]], [["luafunc:hijack_space"]])
/// -show:
/// -show:  -- The [[]] string syntax lets you copy key strings directly from 'clink echo'.
/// -show:  -- [["\e[H"]] is much easier than translating to "\"\\e[H\"", for example.
/// -show:  rl.setbinding([["\e[H"]], [[beginning-of-line]])
static int32 set_rl_binding(lua_State* state)
{
    if (!funmap)
        return 0;

    const char* _key = checkstring(state, 1);
    const char* binding = checkstring(state, 2);
    const char* keymap = optstring(state, 3, nullptr);
    if (!_key || !binding)
        return 0;

    Keymap map = keymap ? rl_get_keymap_by_name(keymap) : rl_get_keymap();

    str<> keys;
    unquote_keys(_key, keys);

    int32 result = -1;
    if (!binding)
    {
        result = rl_bind_keyseq_in_map(keys.c_str(), nullptr, map);
    }
    else if (binding[0] == '\'' || binding[0] == '"')
    {
        str<> tmp;
        tmp.concat(binding + 1);
        if (tmp.length() && tmp.c_str()[tmp.length() - 1] == binding[0])
            tmp.truncate(tmp.length() - 1);

        result = rl_macro_bind(keys.c_str(), tmp.c_str(), map);
    }
    else
    {
        rl_command_func_t* func = rl_named_function(binding);
        result = rl_bind_keyseq_in_map(keys.c_str(), func, map);
        if (!func)
            result = -1;
    }

    lua_pushboolean(state, result >= 0);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.invokecommand
/// -ver:   1.1.26
/// -arg:   command:string
/// -arg:   [count:integer]
/// -ret:   boolean | nil
/// Invokes a Readline command named <span class="arg">command</span>.  May only
/// be used within a <a href="#luakeybindings">luafunc: key binding</a>.
///
/// <span class="arg">count</span> is optional and defaults to 1 if omitted.
///
/// Returns true if the named command succeeds, false if the named command
/// fails, or nil if the named command doesn't exist.
///
/// Warning:  Invoking more than one Readline command in a luafunc: key binding
/// could have unexpected results, depending on which commands are invoked.
static int32 invoke_command(lua_State* state)
{
    LUA_ONLYONMAIN(state, "rl.invokecommand");

    if (!lua_state::is_in_luafunc())
        return luaL_error(state, LUA_QL("rl.invokecommand") " may only be used in a " LUA_QL("luafunc:") " key binding");

    const char* command = checkstring(state, 1);
    if (!command)
        return 0;

    // Must force update_internal() in case a completion command is invoked (or
    // anything uses line_state).  Updating matches in alternative_matches() is
    // too late, because the word break info needs to have already been updated
    // before alternative_matches() is reached.  The update_internal() stuff
    // already is optimized to do nothing if the input line hasn't changed since
    // the last time update_internal() was called.
    force_update_internal(false/*restrict*/);

    if (*command == '"')
    {
        str<> tmp(command + 1);
        if (tmp.length() && tmp[tmp.length() - 1] == '"')
            tmp.truncate(tmp.length() - 1);

        if (!macro_hook_func(tmp.c_str()))
        {
            int32 len = 0;
            char* macro = static_cast<char*>(malloc(tmp.length() * 2 + 1));
            if (rl_translate_keyseq(tmp.c_str(), macro, &len))
            {
                free(macro);
                return 0;
            }
            _rl_with_macro_input(macro);
        }

        lua_pushinteger(state, true);
        return 1;
    }

    rl_command_func_t *func = rl_named_function(command);
    if (func == nullptr)
        return 0;

    int32 isnum;
    int32 count = int32(lua_tointegerx(state, 2, &isnum));
    int32 self_insert = (func == rl_insert);
    int32 err = func(isnum ? count : 1, self_insert ? rl_executing_key : 0);

    override_rl_last_func(func);

    lua_pushinteger(state, !err);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.getlastcommand
/// -ver:   1.1.40
/// -ret:   string, function
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
/// -show:  local last_rl_func, last_lua_func = rl.getlastcommand()
static int32 get_last_command(lua_State* state)
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
/// -ver:   1.1.40
/// -arg:   matches:table
/// -arg:   [type:string]
/// -ret:   integer, boolean
/// Provides an alternative set of matches for the current word.  This discards
/// any matches that may have already been collected and uses
/// <span class="arg">matches</span> for subsequent Readline completion commands
/// until any action that normally resets the matches (such as moving the cursor
/// or editing the input line).
///
/// The syntax is the same as for
/// <a href="#builder:addmatches()">builder:addmatches()</a> with one addition:
/// You can add a <code>"nosort"</code> key to the
/// <span class="arg">matches</span> table to disable sorting the matches.
///
/// <pre><code class="lua">local matches = {}<br/>matches["nosort"] = true<br/>rl.setmatches(matches)</code></pre>
///
/// This function may (only) be used by a
/// <a href="#luakeybindings">luafunc: key binding</a> to provide matches based
/// on some special criteria.  For example, a key binding could collect numbers
/// from the current screen buffer (such as issue numbers, commit hashes, line
/// numbers, etc) and provide them to Readline as matches, making it convenient
/// to grab a number from the screen and insert it as a command line argument.
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
/// -show:  &nbsp;   local _,last_luafunc = rl.getlastcommand()
/// -show:  &nbsp;   if last_luafunc ~= "completenumbers" then
/// -show:  &nbsp;       -- Collect numbers from the screen (minimum of three digits).
/// -show:  &nbsp;       -- The numbers can be any base up to hexadecimal (decimal, octal, etc).
/// -show:  &nbsp;       local matches = console.screengrab("[^%w]*(%w%w[%w]+)", "^%x+$")
/// -show:  &nbsp;       -- They're already sorted by distance from the input line.
/// -show:  &nbsp;       matches["nosort"] = true
/// -show:  &nbsp;       rl.setmatches(matches)
/// -show:  &nbsp;   end
/// -show:
/// -show:  &nbsp;   rl.invokecommand("old-menu-complete")
/// -show:  end
static int32 set_matches(lua_State* state)
{
    LUA_ONLYONMAIN(state, "rl.setmatches");

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

    {
        save_stack_top ss(state);

        lua_getglobal(state, "clink");
        lua_pushliteral(state, "_reset_display_filter");
        lua_rawget(state, -2);
        if (lua_state::pcall(state, 0, 0) != 0)
            return 0;
    }

    rl_last_func = nullptr;

    match_builder builder(*matches);
    match_builder_lua builder_lua(builder);

    return builder_lua.do_add_matches(state, false/*self_on_stack*/);
}

//------------------------------------------------------------------------------
/// -name:  rl.getkeybindings
/// -ver:   1.2.16
/// -arg:   raw:boolean
/// -arg:   [mode:integer]
/// -ret:   table
/// Returns key bindings in a table with the following scheme:
/// -show:  local t = rl.getkeybindings()
/// -show:  -- t[index].key         [string] Key name.
/// -show:  -- t[index].binding     [string] Command or macro bound to the key.
/// -show:  -- t[index].desc        [string] Description of the command.
/// -show:  -- t[index].category    [string] Category of the command.
///
/// When <span class="arg">raw</span> is true, the key names are the literal key
/// sequences without being converted to user-friendly key names.
///
/// The optional <span class="arg">mode</span> specifies bit flags that control
/// how the returned table is sorted, and whether it includes commands that are
/// not bound to any key.
///
/// <table>
/// <tr><th>Mode</th><th>Description</th></tr>
/// <tr><td>0</td><td>Key bindings, sorted by key (this is the default).</td></tr>
/// <tr><td>1</td><td>Key bindings, sorted by category and then key.</td></tr>
/// <tr><td>4</td><td>All commands, sorted by key.</td></tr>
/// <tr><td>5</td><td>All commands, sorted by category and then key.</td></tr>
/// </table>
///
/// The following example demonstrates using this function in a
/// <a href="#luakeybindings">luafunc: key binding</a> to invoke
/// <a href="#clink.popuplist">clink.popuplist()</a> to show a searchable list
/// of key bindings, and then invoke whichever key binding is selected.
/// -show:  function luafunc_showkeybindings(rl_buffer)
/// -show:  &nbsp;   local bindings = rl.getkeybindings()
/// -show:  &nbsp;   if #bindings <= 0 then
/// -show:  &nbsp;       rl_buffer:refreshline()
/// -show:  &nbsp;       return
/// -show:  &nbsp;   end
/// -show:
/// -show:  &nbsp;   local items = {}
/// -show:  &nbsp;   for _,kb in ipairs(bindings) do
/// -show:  &nbsp;       table.insert(items, {
/// -show:  &nbsp;           value = kb.binding,     -- Return the binding when selected, so it can be invoked.
/// -show:  &nbsp;           display = kb.key,       -- Display the key name.
/// -show:  &nbsp;           description = kb.binding.."\t"..kb.desc -- Also display the command and description.
/// -show:  &nbsp;       })
/// -show:  &nbsp;   end
/// -show:
/// -show:  &nbsp;   -- Show a popup that lists the items from above.
/// -show:  &nbsp;   local binding, _, index = clink.popuplist("Key Bindings", items)
/// -show:  &nbsp;   rl_buffer:refreshline()
/// -show:  &nbsp;   if binding then
/// -show:  &nbsp;       -- Invoke the selected binding (a command or macro).
/// -show:  &nbsp;       rl.invokecommand(binding)
/// -show:  &nbsp;   end
/// -show:  end
struct key_binding_info { str_moveable name; str_moveable binding; const char* desc; const char* cat; };
int32 get_key_bindings(lua_State* state)
{
    bool raw = lua_toboolean(state, 1);
    int32 mode = lua_tointeger(state, 2);

    // Get the key bindings.
    void show_key_bindings(bool friendly, int32 mode, std::vector<key_binding_info>* out);
    std::vector<key_binding_info> bindings;
    show_key_bindings(!raw, mode, &bindings);

    // Copy the result into a lua table.
    lua_createtable(state, int32(bindings.size()), 0);

    str<> out;
    int32 i = 1;
    for (auto const& info : bindings)
    {
        lua_createtable(state, 0, 4);

        lua_pushliteral(state, "key");
        lua_pushlstring(state, info.name.c_str(), info.name.length());
        lua_rawset(state, -3);

        lua_pushliteral(state, "binding");
        lua_pushlstring(state, info.binding.c_str(), info.binding.length());
        lua_rawset(state, -3);

        lua_pushliteral(state, "desc");
        lua_pushstring(state, info.desc ? info.desc : "");
        lua_rawset(state, -3);

        lua_pushliteral(state, "category");
        lua_pushstring(state, info.cat);
        lua_rawset(state, -3);

        lua_rawseti(state, -2, i);

        ++i;
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.getcommandbindings
/// -ver:   1.5.1
/// -arg:   command:string
/// -arg:   raw:boolean
/// -ret:   table | nil
/// Returns key bindings and info for <span class="arg">command</span> in a
/// table with the following scheme:
/// -show:  local t = rl.getcommandbindings("complete")
/// -show:  -- t.desc               [string] Description of the command.
/// -show:  -- t.category           [string] Category of the command.
/// -show:  -- t.keys               [table] Table of strings listing the key names that are bound to the command.
///
/// When <span class="arg">raw</span> is true, the key names are the literal key
/// sequences without being converted to user-friendly key names.
///
/// If <span class="arg">command</span> is neither a command nor a macro string,
/// then nil is returned.
/// -show:  local command = 'complete'
/// -show:  -- or for example '"luafunc:my_macro_function"' or '"some macro text"'
/// -show:  local info = rl.getcommandbindings(command)
/// -show:  if info then
/// -show:  &nbsp;   local info = rl.getcommandbindings(command)
/// -show:  &nbsp;   print("Info for '"..command.."':")
/// -show:  &nbsp;   print("Description is:  "..info.desc)
/// -show:  &nbsp;   print("Category is:     "..info.category)
/// -show:  &nbsp;   if #info.keys > 0 then
/// -show:  &nbsp;       print("Bound to:")
/// -show:  &nbsp;       for _,key in ipairs(info.keys) do
/// -show:  &nbsp;           print("    "..key)
/// -show:  &nbsp;       end
/// -show:  &nbsp;   else
/// -show:  &nbsp;       print("Not bound to any keys.")
/// -show:  &nbsp;   end
/// -show:  else
/// -show:  &nbsp;   print("Command '"..command.."' not recognized.")
/// -show:  end
int32 get_command_bindings(lua_State* state)
{
    const char* command = checkstring(state, 1);
    bool raw = lua_toboolean(state, 2);
    if (!command)
        return 0;

    // Get the key bindings.
    str<> desc;
    str<> category;
    bool get_command_bindings(const char* command, bool friendly, str_base& desc, str_base& category, std::vector<str_moveable>& keys);
    std::vector<str_moveable> keys;
    if (!get_command_bindings(command, !raw, desc, category, keys))
        return 0;

    // Copy the result into a lua table.
    lua_createtable(state, 0, 3);

    lua_pushliteral(state, "desc");
    lua_pushstring(state, desc.c_str());
    lua_rawset(state, -3);

    lua_pushliteral(state, "category");
    lua_pushstring(state, category.c_str());
    lua_rawset(state, -3);

    int32 i = 0;
    lua_pushliteral(state, "keys");
    lua_createtable(state, keys.size(), 0);
    for (auto const& key : keys)
    {
        lua_pushlstring(state, key.c_str(), key.length());
        lua_rawseti(state, -2, ++i);
    }
    lua_rawset(state, -3);

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.getpromptinfo
/// -ver:   1.2.28
/// -ret:   table
/// Returns information about the current prompt and input line.
///
/// <strong>Note:</strong> The <span class="arg">promptline</span> and
/// <span class="arg">inputline</span> fields may be skewed if any additional
/// terminal output has occurred (for example if any
/// <a href="#https://www.lua.org/manual/5.2/manual.html#pdf-print">print()</a>
/// calls have happened, or if <code>rl.getpromptinfo()</code> is used inside a
/// <a href="#clink_onendedit">clink.onendedit()</a> event handler, or any other
/// output that the Readline library wouldn't know about).
///
/// The returned table has the following scheme:
/// -show:  local info = rl.getpromptinfo()
/// -show:  -- info.promptprefix              [string] The prompt string, minus the last line of the prompt string.
/// -show:  -- info.promptprefixlinecount     [integer] Number of lines in the promptprefix string.
/// -show:  -- info.prompt                    [string] The last line of the prompt string.
/// -show:  -- info.rprompt                   [string or nil] The right side prompt (or nil if none).
/// -show:  -- info.promptline                [integer] Console line on which the prompt starts.
/// -show:  -- info.inputline                 [integer] Console line on which the input text starts.
/// -show:  -- info.inputlinecount            [integer] Number of lines in the input text.
static int32 get_prompt_info(lua_State* state)
{
    if (prompt_filter::is_filtering())
        return luaL_error(state, LUA_QL("rl.getpromptinfo") " may not be used during prompt filtering");

    lua_createtable(state, 0, 7);

    const char* prefix = rl_get_local_prompt_prefix();
    const char* prompt = rl_get_local_prompt();

    int32 prefix_lines = count_prompt_lines(prefix);
    int32 prompt_lines = count_prompt_lines(prompt);

    lua_pushliteral(state, "promptprefix");
    lua_pushstring(state, prefix);
    lua_rawset(state, -3);

    lua_pushliteral(state, "promptprefixlinecount");
    lua_pushinteger(state, prefix_lines);
    lua_rawset(state, -3);

    lua_pushliteral(state, "prompt");
    lua_pushstring(state, prompt);
    lua_rawset(state, -3);

    if (rl_rprompt)
    {
        lua_pushliteral(state, "rprompt");
        lua_pushstring(state, rl_rprompt);
        lua_rawset(state, -3);
    }

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        int32 anchor = csbi.dwCursorPosition.Y - _rl_last_v_pos;
        int32 prompt_line = anchor - prefix_lines;
        int32 input_line = anchor + prompt_lines;

        lua_pushliteral(state, "promptline");
        lua_pushinteger(state, 1 + prompt_line);
        lua_rawset(state, -3);

        lua_pushliteral(state, "inputline");
        lua_pushinteger(state, 1 + input_line);
        lua_rawset(state, -3);

        lua_pushliteral(state, "inputlinecount");
        lua_pushinteger(state, 1 + _rl_vis_botlin - prompt_lines);
        lua_rawset(state, -3);
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.insertmode
/// -ver:   1.2.50
/// -arg:   [insert:boolean]
/// -ret:   boolean
/// Returns true when typing insertion mode is on.
///
/// When the optional <span class="arg">insert</span> argument is passed, this
/// also sets typing insertion mode on or off accordingly.
static int32 getset_insert_mode(lua_State* state)
{
    if (lua_gettop(state) > 0)
    {
        bool ins = lua_toboolean(state, 1);
        _rl_set_insert_mode(ins ? RL_IM_INSERT : RL_IM_OVERWRITE, 0);
    }

    lua_pushboolean(state, !!(rl_insert_mode & RL_IM_INSERT));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.ismodifiedline
/// -ver:   1.2.51
/// -ret:   boolean
/// Returns true when the current input line is a history entry that has been
/// modified (i.e. has an undo list).
///
/// This enables prompt filters to show a "modmark" of their own, as an
/// alternative to the modmark shown when the
/// <code><a href="#configmarkmodifiedlines">mark-modified-lines</a></code>
/// Readline config setting is enabled.
///
/// The following sample illustrates a prompt filter that shows a "modified
/// line" indicator when the current line is a history entry and has been
/// modified.
/// -show:  local p = clink.promptfilter(10)
/// -show:  local normal = "\x1b[m"
/// -show:
/// -show:  local function get_settings_color(name)
/// -show:  &nbsp;   return "\x1b[" .. settings.get(name) .. "m"
/// -show:  end
/// -show:
/// -show:  function p:filter(prompt)
/// -show:  &nbsp;   prompt = os.getcwd()
/// -show:  &nbsp;   if rl.ismodifiedline() then
/// -show:  &nbsp;       -- If the current line is a history entry and has been modified,
/// -show:  &nbsp;       -- then show an indicator.
/// -show:  &nbsp;       prompt = get_settings_color("color.modmark") .. "*" .. normal .. " " .. prompt
/// -show:  &nbsp;   end
/// -show:  &nbsp;   prompt = prompt .. "\n$ "
/// -show:  &nbsp;   return prompt
/// -show:  end
/// -show:
/// -show:  local last_modmark = false
/// -show:
/// -show:  local function modmark_reset()
/// -show:  &nbsp;   -- Reset the remembered state at the beginning of each edit line.
/// -show:  &nbsp;   last_modmark = rl.ismodifiedline()
/// -show:
/// -show:  &nbsp;   -- Turn off `mark-modified-lines` to avoid two modmarks showing up.
/// -show:  &nbsp;   rl.setvariable("mark-modified-lines", "off")
/// -show:  end
/// -show:
/// -show:  local function modmark_refilter()
/// -show:  &nbsp;   -- If the modmark state has changed, refresh the prompt.
/// -show:  &nbsp;   if last_modmark ~= rl.ismodifiedline() then
/// -show:  &nbsp;       last_modmark = rl.ismodifiedline()
/// -show:  &nbsp;       clink.refilterprompt()
/// -show:  &nbsp;   end
/// -show:  end
/// -show:
/// -show:  clink.onbeginedit(modmark_reset)
/// -show:  clink.onaftercommand(modmark_refilter)
static int32 is_modified_line(lua_State* state)
{
    lua_pushboolean(state, current_history() && rl_undo_list);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.getmatchcolor
/// -ver:   1.3.4
/// -arg:   match:string|table
/// -arg:   [type:string]
/// -ret:   string
/// Returns the color string associated with the match.
///
/// The arguments are the same as in
/// <a href="#builder:addmatch">builder:addmatch()</a>.
static int32 get_match_color(lua_State* state)
{
    const char* match;
    match_type type = match_type::none;

    const bool t = lua_istable(state, 1);
    if (t)
    {
        lua_pushliteral(state, "match");
        lua_rawget(state, 1);
        match = checkstring(state, -1);
        if (!match)
            return 0;
        lua_pop(state, 1);

        lua_pushliteral(state, "type");
        lua_rawget(state, 1);
        const char* tmp = optstring(state, -1, nullptr);
        if (tmp)
            type = to_match_type(tmp);
        lua_pop(state, 1);
    }
    else
    {
        match = checkstring(state, 1);

        const char* tmp = optstring(state, 2, nullptr);
        if (tmp)
            type = to_match_type(tmp);
    }

    str<16> color;
    get_match_color(match, type, color);

    lua_pushlstring(state, color.c_str(), color.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.gethistorycount
/// -ver:   1.3.18
/// -ret:   integer
/// Returns the number of history items.
static int32 get_history_count(lua_State* state)
{
    lua_pushinteger(state, history_length);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.gethistoryitems
/// -ver:   1.3.18
/// -ret:   start:integer
/// -ret:   end:integer
/// Returns a table of history items.
///
/// The first history item is 1, and the last history item is
/// <a href="#rl.gethistorycount">rl.gethistorycount()</a>.  For best
/// performance, use <span class="arg">start</span> and
/// <span class="arg">end</span> to request only the range of history items that
/// will be needed.
///
/// Each history item is a table with the following scheme:
/// -show:  local h = rl.gethistoryitems(1, rl.gethistorycount())
/// -show:  -- h.line       [string] The item's command line string.
/// -show:  -- h.time       [integer or nil] The item's time, compatible with os.time().
///
/// <strong>Note:</strong> The time field is omitted if the history item does
/// not have an associated time.
static int32 get_history_items(lua_State* state)
{
    const auto _start = checkinteger(state, 1);
    const auto _end = checkinteger(state, 2);
    if (!_start.isnum() || !_end.isnum())
        return 0;

    int32 start = _start - 1;
    int32 end = _end;
    if (start >= history_length || end < 1)
        return 0;
    if (start < 0)
        start = 0;
    if (end > history_length)
        end = history_length;

    lua_createtable(state, (end - start) - 1, 0);

    HIST_ENTRY const* const* const items = history_list();
    int32 index = 0;
    for (int32 i = start; i < end; ++i)
    {
        lua_createtable(state, 0, 2);

        lua_pushliteral(state, "line");
        lua_pushstring(state, items[i]->line);
        lua_rawset(state, -3);

        if (items[i]->timestamp)
        {
            lua_pushliteral(state, "time");
            lua_pushinteger(state, atoi(items[i]->timestamp));
            lua_rawset(state, -3);
        }

        lua_rawseti(state, -2, ++index);
    }
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.describemacro
/// -ver:   1.3.41
/// -arg:   macro:string
/// -arg:   description:string
/// This associates <span class="arg">description</span> with
/// <span class="arg">macro</span>, to be displayed in the
/// <code><a href="#rlcmd-clink-show-help">clink-show-help</a></code> and
/// <code><a href="#rlcmd-clink-what-is">clink-what-is</a></code> commands.
///
/// This may be used to add a description for a
/// <a href="#luakeybindings">luafunc: key binding</a> macro, or for a keyboard
/// macro.
///
/// The <span class="arg">macro</span> string should include quotes, just like
/// in <a href="#rl.setbinding">rl.setbinding()</a>.  If quotes are not present,
/// they are added automatically.
/// -show:  rl.describemacro([["luafunc:mycommand"]], "Does whatever mycommand does")
/// -show:  rl.describemacro([["\e[Hrem "]], "Insert 'rem ' at the beginning of the line")
/// -show:  rl.setbinding([["\C-o"]], [["luafunc:mycommand"]])
/// -show:  rl.setbinding([["\C-r"]], [["\e[Hrem "]])
/// -show:  -- Press Alt-H to see the list of key bindings and descriptions.
static int32 describe_macro(lua_State* state)
{
    const char* macro = checkstring(state, 1);
    const char* description = checkstring(state, 2);
    if (macro && description)
    {
        str<> tmp;
        const uint32 len = str_len(macro);
        if (macro[0] == '\"' && macro[1] && macro[len - 1] == '\"')
        {
            tmp.concat(macro + 1, len - 2);
            macro = tmp.c_str();
        }

        add_macro_description(macro, description);
    }
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  rl.needquotes
/// -ver:   1.4.8
/// -arg:   text:string
/// -ret:   boolean
/// Returns whether the <span class="arg">text</span> needs quotes to be parsed
/// correctly in a command line.
static int32 need_quotes(lua_State* state)
{
    const char* text = checkstring(state, 1);
    const bool need = (text &&
                       //rl_filename_quote_characters &&
                       _rl_strpbrk(text, rl_filename_quote_characters) != 0);
    lua_pushboolean(state, need);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.islineequal
/// -ver:   1.4.14
/// -arg:   text:string
/// -arg:   [to_cursor:boolean]
/// -ret:   boolean
/// Returns whether the current input line exactly matches
/// <span class="arg">text</span>.  This can be useful in a coroutine that wants
/// to know whether the current input line has already changed, before it starts
/// a potentially slow operation to generate matches.
/// If the optional <span class="arg">to_cursor</span> is true, then only the
/// line up to the current cursor position is compared.
/// -show:  local function matches_func(word, index, line_state, builder)
/// -show:  &nbsp;    -- Delay match generation briefly, to allow coalescing typed letters into
/// -show:  &nbsp;    -- a single query.  This can make auto-suggestions more responsive if the
/// -show:  &nbsp;    -- match generation operation is slow, such as doing a network query.
/// -show:  &nbsp;    local co, ismain = coroutine.running()
/// -show:  &nbsp;    if not ismain then
/// -show:  &nbsp;        local orig_line = line_state:getline()
/// -show:  &nbsp;        orig_line = orig_line:sub(1, line_state:getcursor() - 1)
/// -show:  &nbsp;        -- Yield for 0.2 seconds.
/// -show:  &nbsp;        clink.setcoroutineinterval(co, 0.2)
/// -show:  &nbsp;        coroutine.yield()
/// -show:  &nbsp;        -- Reset the interval back to normal.
/// -show:  &nbsp;        clink.setcoroutineinterval(co, 0)
/// -show:  &nbsp;        -- If the input line changed during the 0.2 seconds, then don't generate
/// -show:  &nbsp;        -- matches, and mark the matches as needing to be regenerated.
/// -show:  &nbsp;        if not rl.islineequal(orig_line, true) then
/// -show:  &nbsp;            builder:setvolatile()
/// -show:  &nbsp;            return {}
/// -show:  &nbsp;        end
/// -show:  &nbsp;    end
/// -show:
/// -show:  &nbsp;    -- Do something slow that generates matches.
/// -show:  &nbsp;    local matches = {}
/// -show:  &nbsp;    local f = io.popen("slow_operation.exe")
/// -show:  &nbsp;    if f then
/// -show:  &nbsp;        for l in f:lines() do
/// -show:  &nbsp;            table.insert(matches, l)
/// -show:  &nbsp;        end
/// -show:  &nbsp;    end
/// -show:  &nbsp;    return matches
/// -show:  end
static int32 is_line_equal(lua_State* state)
{
    const char* text = checkstring(state, 1);
    const bool to_cursor = lua_toboolean(state, 2);
    const int32 len = to_cursor ? rl_point : rl_end;
    const bool equal = (text &&
                        strlen(text) == len &&
                        strncmp(text, rl_line_buffer, len) == 0);
    lua_pushboolean(state, equal);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.translatekey
/// -ver:   1.6.1
/// -arg:   input:string
/// -arg:   form:integer
/// -ret:   string | nil
/// Translates <span class="arg">input</span> to another format, according to
/// the value of <span class="arg">form</span>:
/// format.
///
/// <ul>
/// <li><code>1</code> converts from an input key sequence (as returned by
/// <a href="#console.readinput">console.readinput</a>) to a friendly key name.
/// <li><code>2</code> converts from an input key sequence (as returned by
/// <a href="#console.readinput">console.readinput</a>) to a bindable key
/// sequence string suitable for use with
/// <a href="#rl.setkeybinding">rl.setkeybinding</a>.
/// <li><code>3</code> converts from a key binding sequence (as understood by
/// <a href="#readline-key-bindings">Readline key bindings</a>) to a friendly
/// key name.
/// <li><code>4</code> converts from a key binding sequence (as understood by
/// <a href="#readline-key-bindings">Readline key bindings</a>) to an input key
/// sequence.
/// </ul>
///
/// If the input key sequence string cannot be translated then nil is returned.
static int32 translate_key(lua_State* state)
{
    const char* input = checkstring(state, 1);
    const auto _form = checkinteger(state, 2);
    if (!input || !_form.isnum())
        return 0;
    const int32 form = _form;
    const int32 len = luaL_len(state, 1);

    char* out = nullptr;
    uint32 out_len = 0;
    int32 sort;

    switch (form)
    {
    case 1:                             // Input sequence to friendly name.
    case 2:                             // Input sequence to bindable sequence.
        translate_keyseq(input, len, &out, (form == 1)/*friendly*/, sort);
        out_len = out ? strlen(out) : 0;
        break;
    case 3:                             // Bindable sequence to friendly name.
    case 4:                             // Bindable sequence to input sequence.
        {
            str_moveable keys;
            int32 keys_len;

            if (*input == '"')
            {
                // "New style" bindable sequence to input sequence.
                str<> seq;
                ++input;
                const char* start = input;
                while (*input && *input != '"')
                    ++input;
                seq.concat(start, uint32(input - start));

                keys.reserve(2 * seq.length());
                if (rl_translate_keyseq(seq.c_str(), keys.data(), &keys_len))
                    return 0;
            }
            else
            {
                // "Old style" bindable sequence to input sequence.
                keys_len = rl_translate_old_keyseq(input, &out);
                if (out)
                {
                    keys.reserve(keys_len);
                    memcpy(keys.data(), out, keys_len + 1);
                    free(out);
                    out = nullptr;
                }
            }

            if (form == 3)
            {
                // Input sequence to friendly name.
                translate_keyseq(keys.c_str(), keys_len, &out, true/*friendly*/, sort);
            }
            else
            {
                // Copy input sequence to be returned.
                out = (char*)malloc(keys_len + 1);
                memcpy(out, keys.c_str(), keys_len + 1);
            }
        }
        break;
    }

    const bool ok = (out && *out);
    if (ok)
        lua_pushstring(state, out);

    free(out);
    return ok ? 1 : 0;
}

//------------------------------------------------------------------------------
/// -name:  rl.bracketpromptcodes
/// -ver:   1.6.1
/// -arg:   prompt:string
/// -ret:   string
/// Certain Readline configuration variables need for ANSI escape codes to be
/// bracketed with <code>\1</code> and <code>\2</code>.  This function returns
/// the <span class="arg">prompt</span> string with that bracketing applied.
static int32 bracket_prompt_codes(lua_State* state)
{
    const char* prompt = checkstring(state, 1);
    if (!prompt)
        return 0;

    str<> out;
    const ecma48_processor_flags flags = ecma48_processor_flags::bracket;
    ecma48_processor(prompt, &out, nullptr/*cell_count*/, flags);

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  rl.getinputrcfilename
/// -ver:   1.6.1
/// -ret:   string|nil, string|nil
/// Returns the path and file name of the Readline init file that was loaded, if
/// any.  Also returns the path and file name of the default Readline init file,
/// if any is present.
///
/// See [Init File](#init-file) for more info.
static int32 get_inputrc_file_name(lua_State* state)
{
    static bool s_initialised = !lua_state::is_interpreter();
    if (!s_initialised)
    {
        s_initialised = true;

        int32 id;
        host_context context;
        host_get_app_context(id, context);
        const char* state_dir = context.profile.empty() ? nullptr : context.profile.c_str();
        const char* default_inputrc = context.default_inputrc.empty() ? nullptr : context.default_inputrc.c_str();

        // Optional undocumented argument:  when called in the standalone Lua
        // interpreter, passing false disables loading the user's inputrc, but
        // the default_inputrc (if any) is still loaded.
        const bool no_user = (lua_isboolean(state, 1) && lua_toboolean(state, 1) == false);

        extern void initialise_readline(const char* shell_name, const char* state_dir, const char* default_inputrc, bool no_user);
        initialise_readline("clink", state_dir, default_inputrc, no_user);
    }
#if 0
    else
    {
        _rl_disable_meta_key();
        _rl_set_insert_mode(RL_IM_INSERT, 0);

// TODO:  This is insufficient; load_user_inputrc() does more than just that.
        rl_re_read_init_file(0, 0);
    }
#endif

    // May return nullptr, which turns into nil, which is intended.
    lua_pushstring(state, rl_get_last_init_file());

    int32 id;
    host_context context;
    host_get_app_context(id, context);
    if (context.default_inputrc.empty())
        lua_pushnil(state);
    else
        lua_pushstring(state, context.default_inputrc.c_str());

    return 2;
}



//------------------------------------------------------------------------------
void rl_lua_initialise(lua_state& lua, bool lua_interpreter)
{
    struct {
        int32       always;
        const char* name;
        int32       (*method)(lua_State*);
    } methods[] = {
        { 1, "collapsetilde",           &collapse_tilde },
        { 1, "expandtilde",             &expand_tilde },
        { 1, "getvariable",             &get_rl_variable },
        { 1, "setvariable",             &set_rl_variable },
        { 1, "isvariabletrue",          &is_rl_variable_true },
        { 1, "getbinding",              &get_rl_binding },
        { 1, "setbinding",              &set_rl_binding },
        { 0, "invokecommand",           &invoke_command },
        { 0, "getlastcommand",          &get_last_command },
        { 0, "setmatches",              &set_matches },
        { 1, "getkeybindings",          &get_key_bindings },
        { 1, "getcommandbindings",      &get_command_bindings },
        { 0, "getpromptinfo",           &get_prompt_info },
        { 0, "insertmode",              &getset_insert_mode },
        { 0, "ismodifiedline",          &is_modified_line },
        { 1, "getmatchcolor",           &get_match_color },
        { 0, "gethistorycount",         &get_history_count },
        { 0, "gethistoryitems",         &get_history_items },
        { 0, "describemacro",           &describe_macro },
        { 1, "needquotes",              &need_quotes },
        { 0, "islineequal",             &is_line_equal },
        { 1, "translatekey",            &translate_key },
        { 1, "bracketpromptcodes",      &bracket_prompt_codes },
        { 1, "getinputrcfilename",      &get_inputrc_file_name },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        if (lua_interpreter ? method.always : method.always >= 0)
        {
            lua_pushstring(state, method.name);
            lua_pushcfunction(state, method.method);
            lua_rawset(state, -3);
        }
    }

    lua_setglobal(state, "rl");
}
