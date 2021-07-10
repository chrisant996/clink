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
#include <lib/popup.h>
#include <terminal/screen_buffer.h>
#include <readline/readline.h>

extern "C" {
#include <lua.h>
}

#include <unordered_set>



//------------------------------------------------------------------------------
// Implemented in host.cpp.
/// -name:  clink.print
/// -arg:   ...
/// -show:  clink.print("\x1b[32mgreen\x1b[m \x1b[35mmagenta\x1b[m")
/// -show:  -- Outputs <code>green</code> in green, a space, and <code>magenta</code> in magenta.
/// -show:
/// -show:  local a = "hello"
/// -show:  local world = 73
/// -show:  clink.print("a", a, "world", world)
/// -show:  -- Outputs <code>a       hello   world   73</code>.
/// -show:
/// -show:  clink.print("hello", NONL)
/// -show:  clink.print("world")
/// -show:  -- Outputs <code>helloworld</code>.
/// This works like <code>print()</code>, but this supports ANSI escape codes.
///
/// If the special value <code>NONL</code> is included anywhere in the argument
/// list then the usual trailing newline is omitted.  This can sometimes be
/// useful particularly when printing certain ANSI escape codes.
///
/// <strong>Note:</strong>  In Clink versions before v1.2.11 the
/// <code>clink.print()</code> API exists (undocumented) but accepts exactly one
/// string argument and is therefore not fully compatible with normal
/// <code>print()</code> syntax.  If you use fewer or more than 1 argument or if
/// the argument is not a string, then first checking the Clink version (e.g.
/// <a href="#clink.version_encoded">clink.version_encoded</a>) can avoid
/// runtime errors.

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
#include <compat/config.h>
#include <readline/rlprivate.h>
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
/// -name:  clink.popuplist
/// -arg:   title:string
/// -arg:   items:table
/// -arg:   [index:integer]
/// -ret:   string, boolean, integer
/// Displays a popup list and returns the selected item.  May only be used
/// within a <a href="#luakeybindings">luafunc: key binding</a>.
///
/// <span class="arg">title</span> is required and captions the popup list.
///
/// <span class="arg">items</span> is a table of strings to display, or a table
/// of items with the following scheme:
/// <span class="tablescheme">{ {display:string, value:string, description:string}, ... }</span>.
/// The <code>display</code> field is displayed in the popup list (or if not
/// present then <code>value</code> is displayed).  The <code>value</code> field
/// is returned if the item is chosen.  The <code>description</code> is
/// optional, and is displayed in a dimmed color in a second column.
///
/// <span class="arg">index</span> optionally specifies the default item (or 1
/// if omitted).
///
/// If the popup is canceled or an error occurs, the function returns nil.
///
/// Otherwise the 3 return values are:
///
/// <ul>
/// <li>string indicating the <code>value</code> field from the selected item
/// (or the <code>display</code> field if no value field is present).
/// <li>boolean which is true if the item was selected with <kbd>Shift</kbd> or
/// <kbd>Ctrl</kbd> pressed.
/// <li>integer indicating the index of the selected item in the original
/// <span class="arg">items</span> table.
/// </ul>
static int popup_list(lua_State* state)
{
    if (!lua_state::is_in_luafunc())
        return luaL_error(state, "clink.popuplist may only be used in a " LUA_QL("luafunc:") " key binding");

    enum arg_indices { makevaluesonebased, argTitle, argItems, argIndex};

    const char* title = checkstring(state, argTitle);
    int index = optinteger(state, argIndex, 1) - 1;
    if (!title || !lua_istable(state, argItems))
        return 0;

    int num_items = int(lua_rawlen(state, argItems));
    if (!num_items)
        return 0;

#ifdef DEBUG
    int top = lua_gettop(state);
#endif

    std::vector<autoptr<const char>> items;
    items.reserve(num_items);
    for (int i = 1; i <= num_items; ++i)
    {
        lua_rawgeti(state, argItems, i);

        const char* value = nullptr;
        const char* display = nullptr;
        const char* description = nullptr;

        if (lua_istable(state, -1))
        {
            lua_pushliteral(state, "value");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                value = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "display");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                display = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "description");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                description = lua_tostring(state, -1);
            lua_pop(state, 1);
        }
        else
        {
            display = lua_tostring(state, -1);
        }

        if (!value && !display)
            value = display = "";
        else if (!display)
            display = value;
        else if (!value)
            value = display;

        size_t alloc_size = 3; // NUL terminators.
        alloc_size += strlen(value);
        alloc_size += strlen(display);
        if (description) alloc_size += strlen(description);

        str_moveable s;
        s.reserve(alloc_size);

        {
            char* p = s.data();
            append_string_into_buffer(p, value);
            append_string_into_buffer(p, display);
            append_string_into_buffer(p, description);
        }

        items.emplace_back(s.detach());

        lua_pop(state, 1);
    }

#ifdef DEBUG
    assert(lua_gettop(state) == top);
    assert(num_items == items.size());
#endif

    str<> out;
    if (index > items.size()) index = items.size();
    if (index < 0) index = 0;

    popup_list_result result = do_popup_list(title, &*items.begin(), items.size(), 0, 0, false, false, false, index, out, true/*display_filter*/);
    switch (result)
    {
    case popup_list_result::select:
    case popup_list_result::use:
        lua_pushlstring(state, out.c_str(), out.length());
        lua_pushboolean(state, (result == popup_list_result::use));
        lua_pushinteger(state, index + 1);
        return 3;
    }

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.getsession
/// -ret:   string
/// -show:  local c = os.getalias("clink")
/// -show:  local r = io.popen(c.." --session "..clink.getsession().." history")
/// Returns the current Clink session id.
///
/// This is needed when using
/// <code><span class="hljs-built_in">io</span>.<span class="hljs-built_in">popen</span>()</code>
/// (or similar functions) to invoke <code>clink history</code> or <code>clink
/// info</code> while Clink is installed for autorun.  The popen API spawns a
/// new CMD.exe, which gets a new Clink instance injected, so the history or
/// info command will use the new session unless explicitly directed to use the
/// calling session.
static int get_session(lua_State* state)
{
    str<32> session;
    session.format("%d", GetCurrentProcessId());
    lua_pushlstring(state, session.c_str(), session.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.getansihost
/// -ret:   string
/// Returns a string indicating who Clink thinks will currently handle ANSI
/// escape codes.  This can change based on the <code>terminal.emulation</code>
/// setting.  This always returns <code>"unknown"</code> until the first edit
/// prompt (see <a href="#clink.onbeginedit">clink.onbeginedit()</a>).
///
/// This can be useful in choosing what kind of ANSI escape codes to use, but it
/// is a best guess and is not necessarily 100% reliable.
///
/// <table>
/// <tr><th>Return</th><th>Description</th></tr>
/// <tr><td>"unknown"</td><td>Clink doesn't know.</td></tr>
/// <tr><td>"clink"</td><td>Clink is emulating ANSI support.  256 color and 24 bit color escape
///     codes are mapped to the nearest of the 16 basic colors.</td></tr>
/// <tr><td>"conemu"</td><td>Clink thinks ANSI escape codes will be handled by ConEmu.</td></tr>
/// <tr><td>"ansicon"</td><td>Clink thinks ANSI escape codes will be handled by ANSICON.</td></tr>
/// <tr><td>"winterminal"</td><td>Clink thinks ANSI escape codes will be handled by Windows
///     Terminal.</td></tr>
/// <tr><td>"winconsole"</td><td>Clink thinks ANSI escape codes will be handled by the default
///     console support in Windows, but Clink detected a terminal replacement that won't support 256
///     color or 24 bit color.</td></tr>
/// <tr><td>"winconsolev2"</td><td>Clink thinks ANSI escape codes will be handled by the default
///     console support in Windows, or it might be handled by a terminal replacement that Clink
///     wasn't able to detect.</td></tr>
/// </table>
static int get_ansi_host(lua_State* state)
{
    static const char* const s_handlers[] =
    {
        "unknown",
        "clink",
        "conemu",
        "ansicon",
        "winterminal",
        "winconsolev2",
        "winconsole",
    };

    static_assert(sizeof_array(s_handlers) == size_t(ansi_handler::max), "must match ansi_handler enum");

    size_t handler = size_t(get_current_ansi_handler());
    lua_pushstring(state, s_handlers[handler]);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.translateslashes
/// -arg:   [mode:integer]
/// -ret:   integer
/// -show:  -- This example affects all match generators, by using priority -1 to
/// -show:  -- run first and returning false to let generators continue.
/// -show:  -- To instead affect only one generator, call clink.translateslashes()
/// -show:  -- in its :generate() function and return true.
/// -show:  local force_slashes = clink.generator(-1)
/// -show:  function force_slashes:generate()
/// -show:  &nbsp; clink.translateslashes(2)    -- Convert to slashes.
/// -show:  &nbsp; return false                 -- Allow generators to continue.
/// -show:  end
/// This overrides how Clink translates slashes in completion matches, which is
/// normally determined by the <code>match.translate_slashes</code> setting.
///
/// This is reset every time match generation is invoked, so use a generator to
/// set this.
///
/// The <span class="arg">mode</span> specifies how to translate slashes when
/// generators add matches:
/// <table>
/// <tr><th>Mode</th><th>Description</th></tr>
/// <tr><td><code>0</code></td><td>No translation.</td></tr>
/// <tr><td><code>1</code></td><td>Translate using the system path separator (backslash on Windows).</td></tr>
/// <tr><td><code>2</code></td><td>Translate to slashes (<code>/</code>).</td></tr>
/// <tr><td><code>3</code></td><td>Translate to backslashes (<code>\</code>).</td></tr>
/// </table>
///
/// If <span class="arg">mode</span> is omitted, then the function returns the
/// current slash translation mode without changing it.
///
/// Note:  Clink always generates file matches using the system path separator
/// (backslash on Windows), regardless what path separator may have been typed
/// as input.  Setting this to <code>0</code> does not disable normalizing typed
/// input paths when invoking completion; it only disables translating slashes
/// in custom generators.
static int translate_slashes(lua_State* state)
{
    extern void set_slash_translation(int mode);
    extern int get_slash_translation();

    if (lua_isnoneornil(state, 1))
    {
        lua_pushinteger(state, get_slash_translation());
        return 1;
    }

    bool isnum;
    int mode = checkinteger(state, 1, &isnum);
    if (!isnum)
        return 0;

    if (mode < 0 || mode > 3)
        mode = 1;

    set_slash_translation(mode);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.slash_translation
/// -arg:   type:integer
/// -deprecated: clink.translateslashes
/// Controls how Clink will translate the path separating slashes for the
/// current path being completed. Values for <span class="arg">type</span> are;</br>
/// -1 - no translation</br>
/// 0 - to backslashes</br>
/// 1 - to forward slashes
static int slash_translation(lua_State* state)
{
    if (lua_gettop(state) == 0)
        return 0;

    if (!lua_isnumber(state, 1))
        return 0;

    int mode = int(lua_tointeger(state, 1));
    if (mode < 0)           mode = 0;
    else if (mode == 0)     mode = 3;
    else if (mode == 1)     mode = 2;
    else                    mode = 1;

    extern void set_slash_translation(int mode);
    set_slash_translation(mode);
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int g_prompt_refilter = 0;
static int refilter_prompt(lua_State* state)
{
    g_prompt_refilter++;
    void host_filter_prompt();
    host_filter_prompt();
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int g_prompt_redisplay = 0;
static int get_refilter_redisplay_count(lua_State* state)
{
    lua_pushinteger(state, g_prompt_refilter);
    lua_pushinteger(state, g_prompt_redisplay);
    return 2;
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
        { "popuplist",              &popup_list },
        { "getsession",             &get_session },
        { "getansihost",            &get_ansi_host },
        { "translateslashes",       &translate_slashes },
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
        { "slash_translation",      &slash_translation },
        // UNDOCUMENTED; internal use only.
        { "refilterprompt",         &refilter_prompt },
        { "get_refilter_redisplay_count", &get_refilter_redisplay_count },
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

#ifdef DEBUG
    lua_pushboolean(state, true);
    lua_setfield(state, -2, "DEBUG");
#endif

    lua_setglobal(state, "clink");
}
