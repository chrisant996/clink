// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_input_idle.h"
#include "line_state_lua.h"
#include "line_states_lua.h"
#include "prompt.h"
#include "recognizer.h"
#include "async_lua_task.h"
#include "../../app/src/version.h" // Ugh.

#include <core/base.h>
#include <core/os.h>
#include <core/str_compare.h>
#include <core/str_transform.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>
#include <core/linear_allocator.h>
#include <core/debugheap.h>
#include <lib/popup.h>
#include <lib/cmd_tokenisers.h>
#include <lib/reclassify.h>
#include <lib/matches_lookaside.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <terminal/screen_buffer.h>

extern "C" {
#include <lua.h>
#include <lstate.h>
#include <readline/history.h>
}

#include <share.h>
#include <mutex>



//------------------------------------------------------------------------------
extern int force_reload_scripts();
extern void host_signal_delayed_init();
extern void host_mark_deprecated_argmatcher(const char* name);
extern void set_suggestion(const char* line, unsigned int endword_offset, const char* suggestion, unsigned int offset);
extern void set_refilter_after_resize(bool refilter);
extern const char* get_popup_colors();
extern const char* get_popup_desc_colors();
extern setting_enum g_dupe_mode;

#ifdef _WIN64
static const char c_uninstall_key[] = "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#else
static const char c_uninstall_key[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#endif

#ifdef TRACK_LOADED_LUA_FILES
extern "C" int is_lua_file_loaded(lua_State* state, const char* filename);
#endif

//------------------------------------------------------------------------------
/// -name:  clink.print
/// -ver:   1.2.11
/// -arg:   ...
/// This works like
/// <a href="https://www.lua.org/manual/5.2/manual.html#pdf-print">print()</a>,
/// but this supports ANSI escape codes and Unicode.
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
static int clink_print(lua_State* state)
{
    str<> out;
    bool nl = true;
    bool err = false;

    int n = lua_gettop(state);              // Number of arguments.
    lua_getglobal(state, "NONL");           // Special value `NONL`.
    lua_getglobal(state, "tostring");       // Function to convert to string (reused each loop iteration).

    int printed = 0;
    for (int i = 1; i <= n; i++)
    {
        // Check for magic `NONL` value.
        if (lua_compare(state, -2, i, LUA_OPEQ))
        {
            nl = false;
            continue;
        }

        // Call function to convert arg to a string.
        lua_pushvalue(state, -1);           // Function to be called (tostring).
        lua_pushvalue(state, i);            // Value to print.
        if (lua_state::pcall(state, 1, 1) != 0)
        {
            if (const char* error = lua_tostring(state, -1))
            {
                puts("");
                puts(error);
            }
            return 0;
        }

        // Get result from the tostring call.
        size_t l;
        const char* s = lua_tolstring(state, -1, &l);
        if (s == NULL)
        {
            err = true;
            break;                          // Allow accumulated output to be printed before erroring out.
        }
        lua_pop(state, 1);                  // Pop result.

        // Add tab character to the output.
        if (printed++)
            out << "\t";

        // Add string result to the output.
        out.concat(s, int(l));
    }

    if (g_printer)
    {
        if (nl)
            out.concat("\n");
        g_printer->print(out.c_str(), out.length());
    }
    else
    {
        printf("%s%s", out.c_str(), nl ? "\n" : "");
    }

    if (err)
        return luaL_error(state, LUA_QL("tostring") " must return a string to " LUA_QL("print"));

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.version_encoded
/// -ver:   1.1.10
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
/// -ver:   1.1.10
/// -var:   integer
/// The major part of the Clink version number.
/// For v<strong>1</strong>.2.3.a0f14d the major version is 1.

//------------------------------------------------------------------------------
/// -name:  clink.version_minor
/// -ver:   1.1.10
/// -var:   integer
/// The minor part of the Clink version number.
/// For v1.<strong>2</strong>.3.a0f14d the minor version is 2.

//------------------------------------------------------------------------------
/// -name:  clink.version_patch
/// -ver:   1.1.10
/// -var:   integer
/// The patch part of the Clink version number.
/// For v1.2.<strong>3</strong>.a0f14d the patch version is 3.

//------------------------------------------------------------------------------
/// -name:  clink.version_commit
/// -ver:   1.1.10
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

//------------------------------------------------------------------------------
/// -name:  clink.split
/// -deprecated: string.explode
/// -arg:   str:string
/// -arg:   sep:string
/// -ret:   table



// END -- Clink 0.4.8 API compatibility ----------------------------------------



//------------------------------------------------------------------------------
/// -name:  clink.match_display_filter
/// -deprecated: builder:addmatch
/// -var:   function
/// This is no longer used.
/// -show:  clink.match_display_filter = function(matches)
/// -show:  &nbsp; -- Transform matches.
/// -show:  &nbsp; return matches
/// -show:  end

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
/// -ver:   0.4.9
/// -arg:   text:string
/// -ret:   string
/// This API correctly converts UTF8 strings to lowercase, with international
/// linguistic awareness.
/// -show:  clink.lower("Hello World") -- returns "hello world"
static int to_lowercase(lua_State* state)
{
    return map_string(state, transform_mode::lower);
}

//------------------------------------------------------------------------------
/// -name:  clink.upper
/// -ver:   1.1.5
/// -arg:   text:string
/// -ret:   string
/// This API correctly converts UTF8 strings to uppercase, with international
/// linguistic awareness.
/// -show:  clink.upper("Hello World") -- returns "HELLO WORLD"
static int to_uppercase(lua_State* state)
{
    return map_string(state, transform_mode::upper);
}

//------------------------------------------------------------------------------
static struct popup_del_callback_info
{
    lua_State*      m_state = nullptr;
    int             m_ref = LUA_REFNIL;

    bool empty() const
    {
        return !m_state || m_ref == LUA_REFNIL;
    }

    bool init(lua_State* state, int idx)
    {
        assert(!m_state);
        m_ref = luaL_ref(state, LUA_REGISTRYINDEX);
        if (m_ref != LUA_REFNIL)
            m_state = state;
        return !empty();
    }

    void clear()
    {
        if (m_state && m_ref != LUA_REFNIL)
            luaL_unref(m_state, LUA_REGISTRYINDEX, m_ref);
        m_state = nullptr;
        m_ref = LUA_REFNIL;
    }

    bool call(int index)
    {
        if (empty())
            return false;
        lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_ref);
        lua_pushinteger(m_state, index + 1);
        if (lua_state::pcall(m_state, 1, 1) != 0)
            return false;
        return lua_toboolean(m_state, -1);
    }
} s_del_callback_info;

//------------------------------------------------------------------------------
static bool popup_del_callback(int index)
{
    return s_del_callback_info.call(index);
}

//------------------------------------------------------------------------------
/// -name:  clink.popuplist
/// -ver:   1.2.17
/// -arg:   title:string
/// -arg:   items:table
/// -arg:   [index:integer]
/// -arg:   [del_callback:function]
/// -ret:   string, boolean, integer
/// Displays a popup list and returns the selected item.  May only be used
/// within a <a href="#luakeybindings">luafunc: key binding</a>.
///
/// <span class="arg">title</span> is required and captions the popup list.
///
/// <span class="arg">items</span> is a table of strings to display.
///
/// <span class="arg">index</span> optionally specifies the default item (or 1
/// if omitted).
///
/// <span class="arg">del_callback</span> optionally specifies a callback
/// function to be called when <kbd>Del</kbd> is pressed.  The function receives
/// the index of the selected item.  If the function returns true then the item
/// is deleted from the popup list.  This requires Clink v1.3.41 or higher.
///
/// The function returns one of the following:
/// <ul>
/// <li>nil if the popup is canceled or an error occurs.
/// <li>Three values:
/// <ul>
/// <li>string indicating the <code>value</code> field from the selected item
/// (or the <code>display</code> field if no value field is present).
/// <li>boolean which is true if the item was selected with <kbd>Shift</kbd> or
/// <kbd>Ctrl</kbd> pressed.
/// <li>integer indicating the index of the selected item in the original
/// <span class="arg">items</span> table.
/// </ul>
/// </ul>
///
/// Alternatively, the <span class="arg">items</span> argument can be a table of
/// tables with the following scheme:
/// -show:  {
/// -show:  &nbsp;   {
/// -show:  &nbsp;       value       = "...",   -- Required; this is returned if the item is chosen.
/// -show:  &nbsp;       display     = "...",   -- Optional; displayed instead of value.
/// -show:  &nbsp;       description = "...",   -- Optional; displayed in a dimmed color in a second column.
/// -show:  &nbsp;   },
/// -show:  &nbsp;   ...
/// -show:  }
///
/// The <code>value</code> field is returned if the item is chosen.
///
/// The optional <code>display</code> field is displayed in the popup list
/// instead of the <code>value</code> field.
///
/// The optional <code>description</code> field is displayed in a dimmed color
/// in a second column.  If it contains tab characters (<code>"\t"</code>) the
/// description string is split into multiple columns (up to 3).
///
/// Starting in v1.3.18, if any description contains a tab character, then the
/// descriptions are automatically aligned in a column.
///
/// Otherwise, the descriptions follow immediately after the display field.
/// They can be aligned in a column by making all of the display fields be the
/// same number of character cells.
///
/// Starting in v1.4.0, the <span class="arg">items</span> table may optionally
/// include any of the following fields to customize the popup list.  The color
/// strings must be
/// <a href="https://en.wikipedia.org/wiki/ANSI_escape_code#SGR">SGR parameters</a>
/// and will be automatically converted into the corresponding ANSI escape code.
/// -show:  {
/// -show:  &nbsp;   height          = 20,       -- Preferred height, not counting the border.
/// -show:  &nbsp;   width           = 60,       -- Preferred width, not counting the border.
/// -show:  &nbsp;   reverse         = true,     -- Start at bottom; search upwards.
/// -show:  &nbsp;   colors = {                  -- Override the popup colors using any colors in this table.
/// -show:  &nbsp;       items       = "97;44",  -- The items color (e.g. bright white on blue).
/// -show:  &nbsp;       desc        = "...",    -- The description color.
/// -show:  &nbsp;       border      = "...",    -- The border color (defaults to items color).
/// -show:  &nbsp;       header      = "...",    -- The title color (defaults to border).
/// -show:  &nbsp;       footer      = "...",    -- The footer message color (defaults to border color).
/// -show:  &nbsp;       select      = "...",    -- The selected item color (defaults to reverse video of items color).
/// -show:  &nbsp;       selectdesc  = "...",    -- The selected item description color (defaults to selected item color).
/// -show:  &nbsp;   }
/// -show:  }
static int popup_list(lua_State* state)
{
    if (!lua_state::is_in_luafunc())
        return luaL_error(state, "clink.popuplist may only be used in a " LUA_QL("luafunc:") " key binding");

    enum arg_indices { makevaluesonebased, argTitle, argItems, argIndex, argDelCallback};

    const char* title = checkstring(state, argTitle);
    const bool has_index = !lua_isnoneornil(state, argIndex);
    int index = optinteger(state, argIndex, 1) - 1;
    if (!title || !lua_istable(state, argItems))
        return 0;

    int num_items = int(lua_rawlen(state, argItems));
    if (!num_items)
        return 0;

#ifdef DEBUG
    int top = lua_gettop(state);
#endif

    std::vector<autoptr<const char>> free_items;
    std::vector<const char*> items;
    free_items.reserve(num_items);
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
            append_string_into_buffer(p, description, true/*allow_tabs*/);
        }

        const char* p = s.detach();
        free_items.emplace_back(p);
        items.emplace_back(p);

        lua_pop(state, 1);
    }

#ifdef DEBUG
    assert(lua_gettop(state) == top);
    assert(num_items == items.size());
#endif

    if (index > items.size()) index = items.size();
    if (index < 0) index = 0;

    popup_config config;

    if (lua_isfunction(state, argDelCallback))
    {
        lua_pushvalue(state, argDelCallback);
        if (s_del_callback_info.init(state, -1))
            config.del_callback = popup_del_callback;
    }

    lua_pushliteral(state, "height");
    lua_rawget(state, argItems);
    if (lua_isnumber(state, -1))
    {
        int n = lua_tointeger(state, -1);
        if (n > 0)
            config.height = n;
    }
    lua_pop(state, 1);

    lua_pushliteral(state, "width");
    lua_rawget(state, argItems);
    if (lua_isnumber(state, -1))
    {
        int n = lua_tointeger(state, -1);
        if (n > 0)
            config.width = n;
    }
    lua_pop(state, 1);

    lua_pushliteral(state, "reverse");
    lua_rawget(state, argItems);
    config.reverse = lua_toboolean(state, -1);
    if (config.reverse && !has_index)
        index = num_items - 1;
    lua_pop(state, 1);

    lua_pushliteral(state, "colors");
    lua_rawget(state, argItems);
    if (lua_istable(state, -1))
    {
        const char* s;

        lua_pushliteral(state, "items");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.items = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "desc");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.desc = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "border");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.border = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "header");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.header = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "footer");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.footer = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "select");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.select = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "selectdesc");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.selectdesc = s;
        lua_pop(state, 1);
    }
    lua_pop(state, 1);

    const popup_results results = activate_text_list(title, &*items.begin(), int(items.size()), index, true/*has_columns*/, &config);

    s_del_callback_info.clear();

    switch (results.m_result)
    {
    case popup_result::select:
    case popup_result::use:
        lua_pushlstring(state, results.m_text.c_str(), results.m_text.length());
        lua_pushboolean(state, (results.m_result == popup_result::select));
        lua_pushinteger(state, results.m_index + 1);
        return 3;
    }

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.getpopuplistcolors
/// -ver:   1.4.0
/// -ret:   table
/// Returns the default popup colors in a table with the following scheme:
/// -show:  {
/// -show:      items   = "...",    -- The SGR parameters for the items color.
/// -show:      desc    = "...",    -- The SGR parameters for the description color.
/// -show:  }
static int get_popup_list_colors(lua_State* state)
{
    struct table_t {
        const char* name;
        const char* value;
    };

    lua_createtable(state, 0, 4);
    {
        struct table_t table[] = {
            { "items", get_popup_colors() },
            { "desc", get_popup_desc_colors() },
        };

        for (unsigned int i = 0; i < sizeof_array(table); ++i)
        {
            const char* value = table[i].value;
            if (value[0] == '0' && value[1] == ';')
                value += 2;
            lua_pushstring(state, table[i].name);
            lua_pushstring(state, value);
            lua_rawset(state, -3);
        }
    }

    return 1;

}

//------------------------------------------------------------------------------
/// -name:  clink.getsession
/// -ver:   1.1.44
/// -ret:   string
/// Returns the current Clink session id.
///
/// This is needed when using
/// <code><span class="hljs-built_in">io</span>.<span class="hljs-built_in">popen</span>()</code>
/// (or similar functions) to invoke <code>clink history</code> or <code>clink
/// info</code> while Clink is installed for autorun.  The popen API spawns a
/// new CMD.exe, which gets a new Clink instance injected, so the history or
/// info command will use the new session unless explicitly directed to use the
/// calling session.
/// -show:  local c = os.getalias("clink")
/// -show:  local r = io.popen(c.." --session "..clink.getsession().." history")
static int get_session(lua_State* state)
{
    str<32> session;
    session.format("%d", GetCurrentProcessId());
    lua_pushlstring(state, session.c_str(), session.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.getansihost
/// -ver:   1.1.48
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
/// -ver:   1.2.7
/// -arg:   [mode:integer]
/// -ret:   integer
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
/// -show:  -- This example affects all match generators, by using priority -1 to
/// -show:  -- run first and returning false to let generators continue.
/// -show:  -- To instead affect only one generator, call clink.translateslashes()
/// -show:  -- in its :generate() function and return true.
/// -show:  local force_slashes = clink.generator(-1)
/// -show:  function force_slashes:generate()
/// -show:  &nbsp;   clink.translateslashes(2)  -- Convert to slashes.
/// -show:  &nbsp;   return false               -- Allow generators to continue.
/// -show:  end
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
/// -deprecated: clink.translateslashes
/// -arg:   type:integer
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
/// -name:  clink.reload
/// -ver:   1.2.29
/// Reloads Lua scripts and Readline config file at the next prompt.
static int reload(lua_State* state)
{
    force_reload_scripts();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.reclassifyline
/// -ver:   1.3.9
/// Reclassify the input line text again and refresh the input line display.
static int reclassify_line(lua_State* state)
{
    const bool ismain = (G(state)->mainthread == state);
    if (ismain)
        host_reclassify(reclassify_reason::force);
    else
        lua_input_idle::signal_reclassify();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.refilterprompt
/// -ver:   1.2.46
/// Invoke the prompt filters again and refresh the prompt.
///
/// Note: this can potentially be expensive; call this only infrequently.
extern bool g_filtering_in_progress;
int g_prompt_refilter = 0;
static int refilter_prompt(lua_State* state)
{
    if (g_filtering_in_progress)
        return luaL_error(state, "clink.refilterprompt may not be used within a prompt filter.");

    g_prompt_refilter++;
    void host_filter_prompt();
    host_filter_prompt();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.refilterafterterminalresize
/// -ver:   1.4.0
/// -arg:   refilter:boolean
/// -ret:   boolean
/// Call this with <span class="arg">refilter</span> either nil or true to make
/// Clink automatically rerun prompt filters after the terminal is resized.  The
/// previous value is returned.
///
/// On Windows the terminal is resized while the console program in the terminal
/// (such as CMD) continues to run.  If a console program writes to the terminal
/// while the resize is happening, then the terminal display can become garbled.
/// So Clink waits until the terminal has stayed the same size for at least 1.5
/// seconds, and then it reruns the prompt filters.
///
/// <strong>Use this with caution:</strong>  if the prompt filters have not been
/// designed efficiently, then rerunning them after resizing the terminal could
/// cause responsiveness problems.  Also, if the terminal is resized again while
/// the prompt filters are being rerun, then the terminal display may become
/// garbled.
static int refilter_after_terminal_resize(lua_State* state)
{
    bool refilter = true;
    if (!lua_isnoneornil(state, 1))
        refilter = lua_toboolean(state, 1);

    set_refilter_after_resize(refilter);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.parseline
/// -ver:   1.3.37
/// -arg:   line:string
/// -ret:   table
/// This parses the <span class="arg">line</span> string into a table of
/// commands, with one <a href="#line_state">line_state</a> for each command
/// parsed from the line string.
///
/// The returned table of tables has the following scheme:
/// -show:  local commands = clink.parseline("echo hello & echo world")
/// -show:  -- commands[1].line_state corresponds to "echo hello".
/// -show:  -- commands[2].line_state corresponds to "echo world".
static int parse_line(lua_State* state)
{
    const char* line = checkstring(state, 1);
    if (!line)
        return 0;

    // A word collector is lightweight, and there is currently no need to
    // support tokenisers for anything other than CMD.  So just create a
    // temporary one here.
    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;
    word_collector collector(&command_tokeniser, &word_tokeniser, "\"");

    // Collect words from the whole line.
    std::vector<word> tmp_words;
    unsigned int len = static_cast<unsigned int>(strlen(line));
    collector.collect_words(line, len, 0, tmp_words, collect_words_mode::whole_command);

    // Group words into one line_state per command.
    commands commands;
    commands.set(line, len, 0, tmp_words);

    // Make a deep copy in an object allocated in the Lua heap.  Garbage
    // collection will free it.
    line_states_lua::make_new(state, commands.get_linestates(line, len));
    return 1;
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
// UNDOCUMENTED; internal use only.
static int is_transient_prompt_filter(lua_State* state)
{
    lua_pushboolean(state, prompt_filter::is_filtering());
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int history_suggester(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const int match_prev_cmd = lua_toboolean(state, 2);
    if (!line)
        return 0;

    HIST_ENTRY** history = history_list();
    if (!history || history_length <= 0)
        return 0;

    // 'match_prev_cmd' only works when 'history.dupe_mode' is 'add'.
    if (match_prev_cmd && g_dupe_mode.get() != 0)
        return 0;

    int scanned = 0;
    const DWORD tick = GetTickCount();

    const int scan_min = 200;
    const DWORD ms_max = 50;

    const char* prev_cmd = (match_prev_cmd && history_length > 0) ? history[history_length - 1]->line : nullptr;
    for (int i = history_length; --i >= 0;)
    {
        // Search at least SCAN_MIN entries.  But after that don't keep going
        // unless it's been less than MS_MAX milliseconds.
        if (scanned >= scan_min && !(scanned % 20) && GetTickCount() - tick >= ms_max)
            break;
        scanned++;

        str_iter lhs(line);
        str_iter rhs(history[i]->line);
        int matchlen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(lhs, rhs);

        // lhs isn't exhausted, or rhs is exhausted?  Continue searching.
        if (lhs.more() || !rhs.more())
            continue;

        // Zero matching length?  Is ok with 'match_prev_cmd', otherwise
        // continue searching.
        if (!matchlen && !match_prev_cmd)
            continue;

        // Match previous command, if needed.
        if (match_prev_cmd)
        {
            if (i <= 0 || str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(prev_cmd, history[i - 1]->line) != -1)
                continue;
        }

        // Suggest this history entry.
        lua_pushstring(state, history[i]->line);
        lua_pushinteger(state, 1);
        return 2;
    }

    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int set_suggestion_result(lua_State* state)
{
    bool isnum;
    const char* line = checkstring(state, -4);
    int endword_offset = checkinteger(state, -3, &isnum) - 1;
    if (!line || !isnum)
        return 0;

    const int line_len = strlen(line);
    if (endword_offset < 0 || endword_offset > line_len)
        return 0;

    const char* suggestion = optstring(state, -2, nullptr);
    int offset = optinteger(state, -1, 0, &isnum) - 1;
    if (!isnum || offset < 0 || offset > line_len)
        offset = line_len;

    set_suggestion(line, endword_offset, suggestion, offset);
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int kick_idle(lua_State* state)
{
    extern void kick_idle();
    kick_idle();
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int recognize_command(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const char* word = checkstring(state, 2);
    const bool quoted = lua_toboolean(state, 3);
    if (!line || !word)
        return 0;
    if (!*line || !*word)
        return 0;

    bool ready;
    const recognition recognized = recognize_command(line, word, quoted, ready, nullptr/*file*/);
    lua_pushinteger(state, int(recognized));
    return 1;
}

//------------------------------------------------------------------------------
static str_unordered_map<int> s_cached_path_type;
static linear_allocator s_cached_path_store(2048);
static std::recursive_mutex s_cached_path_type_mutex;
void clear_path_type_cache()
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    s_cached_path_type.clear();
    s_cached_path_store.clear();
}
void add_cached_path_type(const char* full, int type)
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    dbg_ignore_scope(snapshot, "add_cached_path_type");
    unsigned int size = static_cast<unsigned int>(strlen(full) + 1);
    char* key = static_cast<char*>(s_cached_path_store.alloc(size));
    memcpy(key, full, size);
    s_cached_path_type.emplace(key, type);
}
bool get_cached_path_type(const char* full, int& type)
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    const auto& iter = s_cached_path_type.find(full);
    if (iter == s_cached_path_type.end())
        return false;
    type = iter->second;
    return true;
}

//------------------------------------------------------------------------------
class path_type_async_lua_task : public async_lua_task
{
public:
    path_type_async_lua_task(const char* key, const char* src, const char* path)
    : async_lua_task(key, src)
    , m_path(path)
    {}

    int get_path_type() const { return m_type; }

protected:
    void do_work() override
    {
        m_type = os::get_path_type(m_path.c_str());
        add_cached_path_type(m_path.c_str(), m_type);
    }

private:
    str_moveable m_path;
    int m_type = os::path_type_invalid;
};

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int async_path_type(lua_State* state)
{
    const char* path = checkstring(state, 1);
    int timeout = optinteger(state, 2, 0);
    if (!path || !*path)
        return 0;

    str<280> full;
    os::get_full_path_name(path, full);

    int type;
    if (!get_cached_path_type(full.c_str(), type))
    {
        str_moveable key;
        key.format("async||%s", full.c_str());
        std::shared_ptr<async_lua_task> task = find_async_lua_task(key.c_str());
        bool created = !task;
        if (!task)
        {
            str<> src;
            get_lua_srcinfo(state, src);

            task = std::make_shared<path_type_async_lua_task>(key.c_str(), src.c_str(), full.c_str());
            if (task && lua_isfunction(state, 3))
            {
                lua_pushvalue(state, 3);
                int ref = luaL_ref(state, LUA_REGISTRYINDEX);
                task->set_callback(std::make_shared<callback_ref>(ref));
            }

            add_async_lua_task(task);
        }

        if (timeout)
            WaitForSingleObject(task->get_wait_handle(), timeout);

        if (!task->is_complete())
            return 0;

        std::shared_ptr<path_type_async_lua_task> pt_task = std::dynamic_pointer_cast<path_type_async_lua_task>(task);
        if (!pt_task)
            return 0;

        pt_task->disable_callback();

        type = pt_task->get_path_type();
    }

    const char* ret = nullptr;
    switch (type)
    {
    case os::path_type_file:    ret = "file"; break;
    case os::path_type_dir:     ret = "dir"; break;
    default:                    return 0;
    }

    lua_pushstring(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.recognizecommand
/// -ver:   1.3.38
/// -arg:   [line:string]
/// -arg:   word:string
/// -arg:   [quoted:boolean]
/// -ret:   word_class:string, ready:boolean, file:string
/// This reports the input line coloring word classification to use for a
/// command word.  The return value can be passed into
/// <a href="#word_classifications:classifyword">word_classifications:classifyword()</a>
/// as its <span class="arg">word_class</span> argument.
///
/// This is intended for advanced input line coloring purposes.  For example if
/// a script uses <a href="#clink.onfilterinput">clink.onfilterinput()</a> to
/// modify the input text, then it can use this function inside a custom
/// <a href="#classifier_override_line">classifier</a> to look up the color
/// appropriate for the modified input text.
///
/// The <span class="arg">line</span> is optional and may be an empty string or
/// omitted.  When present, it is parsed to check if it would be processed as a
/// <a href="#directory-shortcuts">directory shortcut</a>.
///
/// The <span class="arg">word</span> is a string indicating the word to be
/// analyzed.
///
/// The <span class="arg">quoted</span> is optional.  When true, it indicates
/// the word is quoted and any <code>^</code> characters are taken as-is,
/// rather than treating them as the usual CMD escape character.
///
/// The possible return values for <span class="arg">word_class</span> are:
///
/// <table>
/// <tr><th>Code</th><th>Classification</th><th>Clink Color Setting</th></tr>
/// <tr><td><code>"x"</code></td><td>Executable; used for the first word when it is not a command or doskey alias, but is an executable name that exists.</td><td><code>color.executable</code></td></tr>
/// <tr><td><code>"u"</code></td><td>Unrecognized; used for the first word when it is not a command, doskey alias, or recognized executable name.</td><td><code>color.unrecognized</code></td></tr>
/// <tr><td><code>"o"</code></td><td>Other; used for file names and words that don't fit any of the other classifications.</td><td><code>color.input</code></td></tr>
/// </table>
///
/// The possible return values for <span class="arg">ready</span> are:
///
/// <ul>
/// <li>True if the analysis has completed.</li>
/// <li>False if the analysis has not yet completed (and the returned word class
/// may be a temporary placeholder).</li>
/// </ul>
///
/// The return value for <span class="arg">file</span> is the fully qualified
/// path to the found executable file, if any, or nil.
///
/// <strong>Note:</strong>  This always returns immediately, and it uses a
/// background thread to analyze the <span class="arg">word</span> asynchronously.
/// When the background thread finishes analyzing the word, Clink automatically
/// redisplays the input line, giving classifiers a chance to call this function
/// again and get the final <span class="arg">word_class</span> result.
static int api_recognize_command(lua_State* state)
{
    int iword = lua_isstring(state, 2) ? 2 : 1;
    int iline = iword - 1;
    const char* line = (iline < 1 || lua_isnil(state, iline)) ? "" : checkstring(state, iline);
    const char* word = checkstring(state, iword);
    const bool quoted = lua_toboolean(state, iword + 1);
    if (iline > 0 && (!line || !*line))
        return 0;
    if (!word || !*word)
        return 0;

    bool ready;
    str<> file;
    const recognition recognized = recognize_command(line, word, quoted, ready, &file);

    char cl = 'o';
    if (int(recognized) < 0)
        cl = 'u';
    else if (int(recognized) > 0)
        cl = 'x';
    lua_pushlstring(state, &cl, 1);
    lua_pushboolean(state, ready);
    if (file.empty())
        lua_pushnil(state);
    else
        lua_pushlstring(state, file.c_str(), file.length());
    return 3;
}

//------------------------------------------------------------------------------
static int generate_from_history(lua_State* state)
{
    HIST_ENTRY** list = history_list();
    if (!list)
        return 0;

    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;
    word_collector collector(&command_tokeniser, &word_tokeniser);
    collector.init_alias_cache();

    save_stack_top ss(state);

    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_generate_from_historyline");
    lua_rawget(state, -2);

    while (*list)
    {
        const char* buffer = (*list)->line;
        unsigned int len = static_cast<unsigned int>(strlen(buffer));

        // Collect one line_state for each command in the line.
        std::vector<word> words;
        commands commands;
        collector.collect_words(buffer, len, len/*cursor*/, words, collect_words_mode::whole_command);
        commands.set(buffer, len, 0, words);

        for (const line_state& line : commands.get_linestates(buffer, len))
        {
            // clink._generate_from_historyline
            lua_pushvalue(state, -1);

            // line_state
            line_state_lua line_lua(line);
            line_lua.push(state);

            if (lua_state::pcall(state, 1, 0) != 0)
                break;
        }

        list++;
    }

    return 0;
}

//------------------------------------------------------------------------------
static int api_reset_generate_matches(lua_State* state)
{
    extern void reset_generate_matches();
    reset_generate_matches();
    return 0;
}

//------------------------------------------------------------------------------
static int mark_deprecated_argmatcher(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (name)
        host_mark_deprecated_argmatcher(name);
    return 0;
}

//------------------------------------------------------------------------------
static int signal_delayed_init(lua_State* state)
{
    lua_input_idle::signal_delayed_init();
    return 0;
}

//------------------------------------------------------------------------------
static int is_cmd_command(lua_State* state)
{
    const char* word = checkstring(state, 1);
    if (!word)
        return 0;

    lua_pushboolean(state, is_cmd_command(word));
    return 1;
}

//------------------------------------------------------------------------------
static int get_installation_type(lua_State* state)
{
    // Open the Uninstall key.

    HKEY hkey;
    wstr<> where(c_uninstall_key);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey))
    {
failed:
        lua_pushliteral(state, "zip");
        return 1;
    }

    // Get binaries path.

    WCHAR long_bin_dir[MAX_PATH * 2];
    {
        str<> tmp;
        if (!os::get_env("=clink.bin", tmp))
            goto failed;

        wstr<> bin_dir(tmp.c_str());
        DWORD len = GetLongPathNameW(bin_dir.c_str(), long_bin_dir, sizeof_array(long_bin_dir));
        if (!len || len >= sizeof_array(long_bin_dir))
            goto failed;

        long_bin_dir[len] = '\0';
    }

    // Enumerate installed programs.

    bool found = false;
    WCHAR install_key[MAX_PATH];
    install_key[0] = '\0';

    for (DWORD index = 0; true; ++index)
    {
        DWORD size = sizeof_array(install_key); // Characters, not bytes, for RegEnumKeyExW.
        if (ERROR_NO_MORE_ITEMS == RegEnumKeyExW(hkey, index, install_key, &size, 0, nullptr, nullptr, nullptr))
            break;

        if (size >= sizeof_array(install_key))
            size = sizeof_array(install_key) - 1;
        install_key[size] = '\0';

        // Ignore if not a Clink installation.
        if (_wcsnicmp(install_key, L"clink_", 6))
            continue;

        HKEY hsubkey;
        if (RegOpenKeyExW(hkey, install_key, 0, MAXIMUM_ALLOWED, &hsubkey))
            continue;

        DWORD type;
        WCHAR location[280];
        DWORD len = sizeof(location); // Bytes, not characters, for RegQueryValueExW.
        LSTATUS status = RegQueryValueExW(hsubkey, L"InstallLocation", NULL, &type, LPBYTE(&location), &len);
        RegCloseKey(hsubkey);

        if (status)
            continue;

        len = len / 2;
        if (len >= sizeof_array(location))
            continue;
        location[len] = '\0';

        // If the uninstall location matches the current binaries directory,
        // then this is a match.
        WCHAR long_location[MAX_PATH * 2];
        len = GetLongPathNameW(location, long_location, sizeof_array(long_location));
        if (len && len < sizeof_array(long_location) && !_wcsicmp(long_bin_dir, long_location))
        {
            found = true;
            break;
        }
    }

    RegCloseKey(hkey);

    if (!found)
        goto failed;

    str<> tmp(install_key);
    lua_pushliteral(state, "exe");
    lua_pushstring(state, tmp.c_str());
    return 2;
}

//------------------------------------------------------------------------------
static int set_install_version(lua_State* state)
{
    const char* key = checkstring(state, 1);
    const char* ver = checkstring(state, 2);
    if (!key || !ver || _strnicmp(key, "clink_", 6))
        return 0;

    if (ver[0] == 'v')
        ver++;

    wstr<> where(c_uninstall_key);
    wstr<> wkey(key);
    where << L"\\" << wkey.c_str();

    HKEY hkey;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey);
    if (status)
        return 0;

    wstr<> name;
    wstr<> version(ver);
    name << L"Clink v" << version.c_str();

    bool ok = true;
    ok = ok && !RegSetValueExW(hkey, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()), (name.length() + 1) * sizeof(*name.c_str()));
    ok = ok && !RegSetValueExW(hkey, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(version.c_str()), (version.length() + 1) * sizeof(*version.c_str()));
    RegCloseKey(hkey);

    if (!ok)
        return 0;

    lua_pushboolean(state, true);
    return 1;
}

//------------------------------------------------------------------------------
#if defined(DEBUG) && defined(_MSC_VER)
static int last_allocation_number(lua_State* state)
{
    lua_pushinteger(state, dbggetallocnumber());
    return 1;
}
#endif

//------------------------------------------------------------------------------
#ifdef TRACK_LOADED_LUA_FILES
static int clink_is_lua_file_loaded(lua_State* state)
{
    const char* filename = checkstring(state, 1);
    if (!filename)
        return 0;

    int loaded = is_lua_file_loaded(state, filename);
    lua_pushboolean(state, loaded);
    return 1;
}
#endif



//------------------------------------------------------------------------------
extern int set_current_dir(lua_State* state);
extern int get_aliases(lua_State* state);
extern int get_current_dir(lua_State* state);
extern int get_env(lua_State* state);
extern int get_env_names(lua_State* state);
extern int get_screen_info(lua_State* state);
extern int is_dir(lua_State* state);
extern int explode(lua_State* state);

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
        { "getpopuplistcolors",     &get_popup_list_colors },
        { "getsession",             &get_session },
        { "getansihost",            &get_ansi_host },
        { "translateslashes",       &translate_slashes },
        { "reload",                 &reload },
        { "reclassifyline",         &reclassify_line },
        { "refilterprompt",         &refilter_prompt },
        { "refilterafterterminalresize", &refilter_after_terminal_resize },
        { "parseline",              &parse_line },
        { "recognizecommand",       &api_recognize_command },
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
        { "split",                  &explode },
        // UNDOCUMENTED; internal use only.
        { "istransientpromptfilter", &is_transient_prompt_filter },
        { "get_refilter_redisplay_count", &get_refilter_redisplay_count },
        { "history_suggester",      &history_suggester },
        { "set_suggestion_result",  &set_suggestion_result },
        { "kick_idle",              &kick_idle },
        { "_recognize_command",     &recognize_command },
        { "_async_path_type",       &async_path_type },
        { "_generate_from_history", &generate_from_history },
        { "_reset_generate_matches", &api_reset_generate_matches },
        { "_mark_deprecated_argmatcher", &mark_deprecated_argmatcher },
        { "_signal_delayed_init",   &signal_delayed_init },
        { "is_cmd_command",         &is_cmd_command },
        { "_get_installation_type", &get_installation_type },
        { "_set_install_version",   &set_install_version },
#if defined(DEBUG) && defined(_MSC_VER)
        { "last_allocation_number", &last_allocation_number },
#endif
#ifdef TRACK_LOADED_LUA_FILES
        { "is_lua_file_loaded",     &clink_is_lua_file_loaded },
#endif
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pushliteral(state, "version_encoded");
    lua_pushinteger(state, CLINK_VERSION_MAJOR * 10000000 +
                           CLINK_VERSION_MINOR *    10000 +
                           CLINK_VERSION_PATCH);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_major");
    lua_pushinteger(state, CLINK_VERSION_MAJOR);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_minor");
    lua_pushinteger(state, CLINK_VERSION_MINOR);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_patch");
    lua_pushinteger(state, CLINK_VERSION_PATCH);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_commit");
    lua_pushstring(state, AS_STR(CLINK_COMMIT));
    lua_rawset(state, -3);


#ifdef DEBUG
    lua_pushliteral(state, "DEBUG");
    lua_pushboolean(state, true);
    lua_rawset(state, -3);
#endif

    lua_setglobal(state, "clink");
}
