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
#include "lib/matches.h"
#include "match_builder_lua.h"

#include <vector>

extern "C" {
#include "lua.h"
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
extern int              _rl_completion_case_map;
extern const char*      rl_readline_name;
extern int              _rl_last_v_pos;
}

extern matches* get_mutable_matches(bool nosort=false);
extern const char* get_last_luafunc();
extern void override_rl_last_func(rl_command_func_t* func);

extern int count_prompt_lines(const char* prompt_prefix, int len);



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
    const char* path = checkstring(state, 1);
    if (!path)
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
    const char* path = checkstring(state, 1);
    if (!path)
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
static int set_rl_variable(lua_State* state)
{
    const char* name = checkstring(state, 1);
    const char* value = checkstring(state, 2);
    if (!name || !value)
        return 0;

    const char* rl_cvar = rl_variable_value(name);
    if (rl_cvar == nullptr)
        return 0;

    int failed = rl_variable_bind(name, value);
    lua_pushboolean(state, !failed);
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
/// -name:  rl.invokecommand
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
static int invoke_command(lua_State* state)
{
    if (!lua_state::is_in_luafunc())
        return luaL_error(state, "rl.invokecommand may only be used in a " LUA_QL("luafunc:") " key binding");

    const char* command = checkstring(state, 1);
    if (!command)
        return 0;

    if (*command == '"')
    {
        str<> tmp(command + 1);
        if (tmp[tmp.length() - 1] == '"')
            tmp.truncate(tmp.length() - 1);

        extern int macro_hook_func(const char* macro);
        if (!macro_hook_func(tmp.c_str()))
        {
            int len = 0;
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

    int isnum;
    int count = int(lua_tointegerx(state, 2, &isnum));
    int err = func(isnum ? count : 1, 0/*invoking_key*/);

    override_rl_last_func(func);

    lua_pushinteger(state, !err);
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
/// May only be used within a
/// <a href="#luakeybindings">luafunc: key binding</a>.
///
/// The syntax is the same as for
/// <a href="#builder:addmatches()">builder:addmatches()</a> with one addition:
/// You can add a <code>"nosort"</code> key to the
/// <span class="arg">matches</span> table to disable sorting the matches.
///
/// <pre><code class="lua">local matches = {}<br/>matches["nosort"] = true<br/>rl.setmatches(matches)</code></pre>
///
/// This function can be used by a
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

    {
        save_stack_top ss(state);

        lua_getglobal(state, "clink");
        lua_pushliteral(state, "_reset_display_filter");
        lua_rawget(state, -2);
        if (lua_state::pcall(state, 0, 0) != 0)
        {
            puts(lua_tostring(state, -1));
            return 0;
        }
    }

    rl_last_func = nullptr;

    match_builder builder(*matches);
    match_builder_lua builder_lua(builder);

    return builder_lua.add_matches(state);
}

//------------------------------------------------------------------------------
/// -name:  rl.getkeybindings
/// -arg:   raw:boolean
/// -ret:   table
/// -show:  function luafunc_showkeybindings(rl_buffer)
/// -show:      local bindings = rl.getkeybindings()
/// -show:      if #bindings <= 0 then
/// -show:          rl_buffer:refreshline()
/// -show:          return
/// -show:      end
/// -show:
/// -show:      local items = {}
/// -show:      for _,kb in ipairs(bindings) do
/// -show:          table.insert(items, { value=kb.binding, display=kb.key, description=kb.binding.."\t"..kb.desc })
/// -show:      end
/// -show:
/// -show:      local binding, _, index = clink.popuplist("Key Bindings", items)
/// -show:      rl_buffer:refreshline()
/// -show:      if binding then
/// -show:          rl.invokecommand(binding)
/// -show:      end
/// -show:  end
/// Returns key bindings in a table with the following scheme:
/// <span class="tablescheme">{ {key:string, binding:string, desc:string, category:string}, ... }</span>.
///
/// The following example demonstrates using this function in a
/// <a href="#luakeybindings">luafunc: key binding</a> to invoke
/// <a href="#clink.popuplist">clink.popuplist()</a> to show a searchable list
/// of key bindings, and then invoke whichever key binding is selected.
struct key_binding_info { str_moveable name; str_moveable binding; const char* desc; const char* cat; };
int get_key_bindings(lua_State* state)
{
    bool raw = lua_toboolean(state, 1);

    // Get the key bindings.
    void show_key_bindings(bool friendly, int mode, std::vector<key_binding_info>* out);
    std::vector<key_binding_info> bindings;
    show_key_bindings(!raw, 0, &bindings);

    // Copy the result into a lua table.
    lua_createtable(state, int(bindings.size()), 0);

    str<> out;
    int i = 1;
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
/// -name:  rl.getpromptinfo
/// -ret:   table
/// Returns information about the current prompt and input line.
///
/// Note: the <span class="arg">promptline</span> and
/// <span class="arg">inputline</span> fields may be skewed if any additional
/// terminal output has occurred (for example if any <code>print()</code> calls
/// have happened, or if <code>rl.getpromptinfo()</code> is used inside a
/// <a href="#clink_onendedit">clink.onendedit()</a> event handler, or any other
/// output that the Readline library wouldn't know about).
///
/// The returned table has the following scheme:
/// -show:  {
/// -show:  &nbsp; promptprefix,            -- [string] the prompt string, minus the last line of the prompt string
/// -show:  &nbsp; promptprefixlinecount,   -- [integer] number of lines in the promptprefix string
/// -show:  &nbsp; prompt,                  -- [string] the last line of the prompt string
/// -show:  &nbsp; rprompt,                 -- [string or nil] the right side prompt (or nil if none)
/// -show:  &nbsp; promptline,              -- [integer] console line on which the prompt starts
/// -show:  &nbsp; inputline,               -- [integer] console line on which the input text starts
/// -show:  &nbsp; inputlinecount,          -- [integer] number of lines in the input text
/// -show:  }
static int get_prompt_info(lua_State* state)
{
    lua_createtable(state, 0, 7);

    str_moveable bracketed_prefix;
    const char* prefix = rl_get_local_prompt_prefix();
    if (prefix)
    {
        ecma48_processor_flags flags = ecma48_processor_flags::bracket;
        ecma48_processor(prefix, &bracketed_prefix, nullptr/*cell_count*/, flags);
    }

    int prefix_lines = count_prompt_lines(bracketed_prefix.c_str(), bracketed_prefix.length());

    lua_pushliteral(state, "promptprefix");
    lua_pushstring(state, prefix);
    lua_rawset(state, -3);

    lua_pushliteral(state, "promptprefixlinecount");
    lua_pushinteger(state, prefix_lines);
    lua_rawset(state, -3);

    lua_pushliteral(state, "prompt");
    lua_pushstring(state, rl_get_local_prompt());
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
        int input_line = csbi.dwCursorPosition.Y - _rl_last_v_pos;

        lua_pushliteral(state, "promptline");
        lua_pushinteger(state, 1 + input_line - prefix_lines);
        lua_rawset(state, -3);

        lua_pushliteral(state, "inputline");
        lua_pushinteger(state, 1 + input_line);
        lua_rawset(state, -3);

        lua_pushliteral(state, "inputlinecount");
        lua_pushinteger(state, 1 + _rl_vis_botlin);
        lua_rawset(state, -3);
    }

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
        { "setvariable",            &set_rl_variable },
        { "isvariabletrue",         &is_rl_variable_true },
        { "invokecommand",          &invoke_command },
        { "getlastcommand",         &get_last_command },
        { "setmatches",             &set_matches },
        { "getkeybindings",         &get_key_bindings },
        { "getpromptinfo",          &get_prompt_info },
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
